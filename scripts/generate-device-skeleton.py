#!/usr/bin/env python3
"""Generate a NestDAQ FairMQ device skeleton."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


CLASS_NAME_PATTERN = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
PROCESSING_MODES = ("conditional-run", "run", "on-data")
CHANNEL_KINDS = ("input", "output", "dqm")
DEFAULT_CHANNEL_OPTIONS = {
    "input": ("in-chan-name", "in"),
    "output": ("out-chan-name", "out"),
    "dqm": ("dqm-chan-name", "dqm"),
}


@dataclass(frozen=True)
class TemplateSpec:
    output_pattern: str
    text: str


BUILTIN_TEMPLATES = {
    "Device.hpp.in": TemplateSpec(
        output_pattern="{class_name}.hpp",
        text=r"""#pragma once

/**
 * @file @HEADER_FILE@
 * @brief Minimal NestDAQ FairMQ device skeleton.
 */

#include <fairmq/Device.h>
@HEADER_INCLUDES@

@NAMESPACE_OPEN@

class @CLASS_NAME@ : public fair::mq::Device
{
public:
@OPTION_KEY_DECLARATIONS@

    @CLASS_NAME@() = default;
    @CLASS_NAME@(const @CLASS_NAME@&) = delete;
    @CLASS_NAME@& operator=(const @CLASS_NAME@&) = delete;
    @CLASS_NAME@(@CLASS_NAME@&&) = delete;
    @CLASS_NAME@& operator=(@CLASS_NAME@&&) = delete;
    ~@CLASS_NAME@() override = default;

private:
@MEMBER_DECLARATIONS@

@PROCESSING_DECLARATIONS@
};

@NAMESPACE_CLOSE@
""",
    ),
    "Device.cpp.in": TemplateSpec(
        output_pattern="{class_name}.cpp",
        text=r"""/** @file
 *  @brief Implements the @CLASS_NAME@ NestDAQ device skeleton.
 */

@SOURCE_INCLUDES@

#include <nestdaq/runDevice.h>

#include "@HEADER_FILE@"

namespace bpo = boost::program_options;

auto addCustomOptions(bpo::options_description& options) -> void
{
@CUSTOM_OPTION_DEFINITIONS@
}

auto getDevice(const fair::mq::ProgOptions& /*config*/) -> std::unique_ptr<fair::mq::Device>
{
    return std::make_unique<@QUALIFIED_CLASS_NAME@>();
}

@PROCESSING_DEFINITIONS@
""",
    ),
    "CMakeLists.txt.in": TemplateSpec(
        output_pattern="CMakeLists.txt",
        text=r"""cmake_minimum_required(VERSION 3.22)

project(@CLASS_NAME@ LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
elseif(CMAKE_CXX_STANDARD LESS 17)
  message(FATAL_ERROR "A minimum CMAKE_CXX_STANDARD of 17 is required.")
endif()
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(NestDAQ REQUIRED CONFIG)

add_executable(@CLASS_NAME@
  @SOURCE_FILE@
)

target_link_libraries(@CLASS_NAME@ PUBLIC
  NestDAQ::NestDAQ
)

install(TARGETS @CLASS_NAME@
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
""",
    ),
    "README.md.in": TemplateSpec(
        output_pattern="README.md",
        text=r"""# @CLASS_NAME@

This directory was generated from the NestDAQ device skeleton template.

Generation choices:

```text
@GENERATION_SUMMARY@
```

## Generated Files

`generate-device-skeleton.py` created this directory from its built-in
templates, replacing template placeholders and writing concrete device files.

| Template | Generated file |
| :-- | :-- |
| `Device.hpp.in` | `@HEADER_FILE@` |
| `Device.cpp.in` | `@SOURCE_FILE@` |
@CMAKE_GENERATED_ROW@| `README.md.in` | `README.md` |

The placeholders `@CLASS_NAME@`, `@HEADER_FILE@`, and `@SOURCE_FILE@` have
already been replaced for this device. Add device-specific options in
`addCustomOptions()` and implement the FairMQ lifecycle hooks in
`@SOURCE_FILE@`.

@CMAKE_BUILD_SECTION@

For examples with data channels and telemetry instrumentation, see the NestDAQ
`examples/` directory.
""",
    ),
}


@dataclass(frozen=True)
class ChannelSpec:
    kind: str
    option_key: str
    default_name: str


@dataclass(frozen=True)
class GenerationConfig:
    class_name: str
    processing_mode: str
    input_channel: ChannelSpec | None
    output_channel: ChannelSpec | None
    dqm_channel: ChannelSpec | None
    multipart_input: bool
    multipart_output: bool
    multipart_dqm: bool
    drain_input: bool
    no_poll: frozenset[str]
    namespace_name: str | None
    generate_cmake: bool
    generate_readme: bool


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate a NestDAQ FairMQ device skeleton.")
    parser.add_argument("class_name", nargs="?", help="C++ device class name to generate")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="Output directory. Defaults to ./CLASS_NAME.",
    )
    parser.add_argument(
        "--processing-mode",
        choices=PROCESSING_MODES,
        default="conditional-run",
        help="Main data-processing style to generate. Default: conditional-run.",
    )
    input_channel_group = parser.add_mutually_exclusive_group()
    input_channel_group.add_argument(
        "--input-channel",
        help="Override the generated input channel. Use KEY:DEFAULT_NAME, :DEFAULT_NAME, or DEFAULT_NAME.",
    )
    input_channel_group.add_argument(
        "--no-input-channel",
        action="store_true",
        help="Do not generate input-channel code.",
    )
    output_channel_group = parser.add_mutually_exclusive_group()
    output_channel_group.add_argument(
        "--output-channel",
        help="Override the generated output channel. Use KEY:DEFAULT_NAME, :DEFAULT_NAME, or DEFAULT_NAME.",
    )
    output_channel_group.add_argument(
        "--no-output-channel",
        action="store_true",
        help="Do not generate output-channel code.",
    )
    dqm_channel_group = parser.add_mutually_exclusive_group()
    dqm_channel_group.add_argument(
        "--dqm-channel",
        help="Override the generated DQM channel. Use KEY:DEFAULT_NAME, :DEFAULT_NAME, or DEFAULT_NAME.",
    )
    dqm_channel_group.add_argument(
        "--no-dqm-channel",
        action="store_true",
        help="Do not generate data quality monitor (DQM) channel code.",
    )
    parser.add_argument(
        "--multipart-input",
        action="store_true",
        help="Generate multipart receive/OnData examples for the input channel.",
    )
    parser.add_argument(
        "--single-output",
        action="store_true",
        help="Generate single-message output examples. Output is multipart by default.",
    )
    parser.add_argument(
        "--single-dqm",
        action="store_true",
        help="Generate single-message DQM examples. DQM is multipart by default.",
    )
    parser.add_argument(
        "--no-drain-input",
        action="store_true",
        help="Do not generate PostRun input drain code.",
    )
    parser.add_argument(
        "--no-poll",
        default="",
        help="Comma-separated channel kinds to exclude from FairMQ polling: input,output,dqm.",
    )
    parser.add_argument(
        "--no-namespace",
        action="store_true",
        help="Generate the device class in the global namespace instead of namespace nestdaq.",
    )
    parser.add_argument(
        "--no-cmake",
        action="store_true",
        help="Do not generate CMakeLists.txt.",
    )
    parser.add_argument(
        "--no-readme",
        action="store_true",
        help="Do not generate README.md.",
    )
    parser.add_argument(
        "--interactive",
        action="store_true",
        help="Prompt for generation choices. Prompts are used only when this option is set.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Overwrite existing generated files.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print files that would be generated without writing them.",
    )
    return parser.parse_args()


def prompt_text(prompt: str, default: str | None = None) -> str:
    suffix = f" [{default}]" if default is not None else ""
    value = input(f"{prompt}{suffix}: ").strip()
    if value:
        return value
    if default is not None:
        return default
    return ""


def prompt_bool(prompt: str, default: bool) -> bool:
    default_text = "y" if default else "n"
    while True:
        value = prompt_text(f"{prompt} (y/n)", default_text).lower()
        if value in ("y", "yes"):
            return True
        if value in ("n", "no"):
            return False
        print("Please answer y or n.", file=sys.stderr)


def apply_interactive(args: argparse.Namespace) -> argparse.Namespace:
    if not args.interactive:
        return args

    if not args.class_name:
        args.class_name = prompt_text("C++ device class name")

    args.processing_mode = prompt_text(
        "Processing mode (conditional-run, run, on-data)", args.processing_mode
    )

    if prompt_bool("Generate input-channel code", not args.no_input_channel):
        args.no_input_channel = False
        args.input_channel = prompt_text(
            "Input channel KEY:DEFAULT_NAME, :DEFAULT_NAME, or DEFAULT_NAME",
            args.input_channel or "in-chan-name:in",
        )
        args.multipart_input = prompt_bool("Use multipart input", args.multipart_input)
        args.no_drain_input = not prompt_bool("Generate PostRun input drain", not args.no_drain_input)
    else:
        args.no_input_channel = True
        args.input_channel = None

    if prompt_bool("Generate output-channel code", not args.no_output_channel):
        args.no_output_channel = False
        args.output_channel = prompt_text(
            "Output channel KEY:DEFAULT_NAME, :DEFAULT_NAME, or DEFAULT_NAME",
            args.output_channel or "out-chan-name:out",
        )
        args.single_output = not prompt_bool("Use multipart output", not args.single_output)
    else:
        args.no_output_channel = True
        args.output_channel = None

    if prompt_bool("Generate DQM-channel code", not args.no_dqm_channel):
        args.no_dqm_channel = False
        args.dqm_channel = prompt_text(
            "DQM channel KEY:DEFAULT_NAME, :DEFAULT_NAME, or DEFAULT_NAME",
            args.dqm_channel or "dqm-chan-name:dqm",
        )
        args.single_dqm = not prompt_bool("Use multipart DQM", not args.single_dqm)
    else:
        args.no_dqm_channel = True
        args.dqm_channel = None

    args.no_poll = prompt_text("Channels not to poll (comma-separated input,output,dqm)", args.no_poll)
    return args


def validate_class_name(class_name: str) -> None:
    if not CLASS_NAME_PATTERN.match(class_name):
        raise ValueError(
            f"invalid C++ class name '{class_name}'; expected pattern {CLASS_NAME_PATTERN.pattern}"
        )


def parse_channel_spec(kind: str, value: str | None) -> ChannelSpec | None:
    if not value:
        return None
    if ":" in value:
        option_key, default_name = value.split(":", 1)
        if not option_key.strip():
            option_key = DEFAULT_CHANNEL_OPTIONS[kind][0]
    else:
        option_key, default_name = DEFAULT_CHANNEL_OPTIONS[kind][0], value
    option_key = option_key.strip()
    default_name = default_name.strip()
    if not option_key or not default_name:
        raise ValueError(f"--{kind}-channel expects KEY:DEFAULT_NAME, :DEFAULT_NAME, or DEFAULT_NAME")
    return ChannelSpec(kind=kind, option_key=option_key, default_name=default_name)


def configured_channel_spec(kind: str, value: str | None, disabled: bool) -> ChannelSpec | None:
    if disabled:
        return None
    if value is None:
        option_key, default_name = DEFAULT_CHANNEL_OPTIONS[kind]
        return ChannelSpec(kind=kind, option_key=option_key, default_name=default_name)
    return parse_channel_spec(kind, value)


def parse_no_poll(value: str) -> frozenset[str]:
    if not value:
        return frozenset()
    items = frozenset(item.strip() for item in value.split(",") if item.strip())
    invalid = sorted(items.difference(CHANNEL_KINDS))
    if invalid:
        raise ValueError(f"invalid --no-poll channel kind(s): {', '.join(invalid)}")
    return items


def build_config(args: argparse.Namespace) -> GenerationConfig:
    if not args.class_name:
        raise ValueError("CLASS_NAME is required unless --interactive provides it")
    validate_class_name(args.class_name)

    if args.processing_mode not in PROCESSING_MODES:
        raise ValueError(f"invalid processing mode: {args.processing_mode}")

    input_channel = configured_channel_spec("input", args.input_channel, args.no_input_channel)
    output_channel = configured_channel_spec("output", args.output_channel, args.no_output_channel)
    dqm_channel = configured_channel_spec("dqm", args.dqm_channel, args.no_dqm_channel)
    no_poll = parse_no_poll(args.no_poll)

    if args.processing_mode == "on-data" and input_channel is None:
        raise ValueError("--processing-mode on-data requires generated input-channel code")
    if args.multipart_input and input_channel is None:
        raise ValueError("--multipart-input requires generated input-channel code")

    return GenerationConfig(
        class_name=args.class_name,
        processing_mode=args.processing_mode,
        input_channel=input_channel,
        output_channel=output_channel,
        dqm_channel=dqm_channel,
        multipart_input=args.multipart_input,
        multipart_output=output_channel is not None and not args.single_output,
        multipart_dqm=dqm_channel is not None and not args.single_dqm,
        drain_input=input_channel is not None and not args.no_drain_input,
        no_poll=no_poll,
        namespace_name=None if args.no_namespace else "nestdaq",
        generate_cmake=not args.no_cmake,
        generate_readme=not args.no_readme,
    )


def selected_templates(config: GenerationConfig) -> dict[str, TemplateSpec]:
    return {
        template_name: template
        for template_name, template in BUILTIN_TEMPLATES.items()
        if (config.generate_cmake or template_name != "CMakeLists.txt.in")
        and (config.generate_readme or template_name != "README.md.in")
    }


def indent(text: str, spaces: int) -> str:
    if not text:
        return ""
    prefix = " " * spaces
    return "\n".join(prefix + line if line else line for line in text.splitlines())


def namespace_open(config: GenerationConfig) -> str:
    if config.namespace_name is None:
        return ""
    return f"namespace {config.namespace_name} {{"


def namespace_close(config: GenerationConfig) -> str:
    if config.namespace_name is None:
        return ""
    return f"}} // namespace {config.namespace_name}"


def qualified_class_name(config: GenerationConfig) -> str:
    if config.namespace_name is None:
        return config.class_name
    return f"{config.namespace_name}::{config.class_name}"


def option_key_lines(config: GenerationConfig) -> list[str]:
    lines: list[str] = []
    if config.input_channel:
        lines.append(f'static constexpr const char* InputChannelName{{"{config.input_channel.option_key}"}};')
    if config.output_channel:
        lines.append(f'static constexpr const char* OutputChannelName{{"{config.output_channel.option_key}"}};')
    if config.dqm_channel:
        lines.append(f'static constexpr const char* DQMChannelName{{"{config.dqm_channel.option_key}"}};')
    if needs_poll_timeout(config):
        lines.append('static constexpr const char* PollTimeoutMS{"poll-timeout-ms"};')
    if config.drain_input:
        lines.append('static constexpr const char* DrainTimeoutMS{"drain-timeout-ms"};')
        lines.append('static constexpr const char* DrainMaxTimeoutCount{"drain-max-timeout-count"};')
    return lines


def needs_poll_timeout(config: GenerationConfig) -> bool:
    return bool(polled_channels(config)) or config.drain_input or bool(config.input_channel and "input" in config.no_poll)


def needs_numeric_option(config: GenerationConfig) -> bool:
    return needs_poll_timeout(config) or config.drain_input


def polled_channels(config: GenerationConfig) -> list[ChannelSpec]:
    channels = []
    for spec in (config.input_channel, config.output_channel, config.dqm_channel):
        if spec is not None and spec.kind not in config.no_poll:
            if config.processing_mode == "on-data" and spec.kind == "input":
                continue
            if config.processing_mode == "run":
                continue
            channels.append(spec)
    return channels


def render_option_key_declarations(config: GenerationConfig) -> str:
    lines = option_key_lines(config)
    if not lines:
        return ""
    body = "\n".join(indent(line, 8) for line in lines)
    return f"""    struct OptionKey {{
{body}
    }};"""


def render_member_declarations(config: GenerationConfig) -> str:
    lines: list[str] = []
    if config.input_channel:
        lines.append("std::string fInputChannelName;")
    if config.output_channel:
        lines.append("std::string fOutputChannelName;")
    if config.dqm_channel:
        lines.append("std::string fDQMChannelName;")
    if needs_poll_timeout(config):
        lines.append("int fPollTimeoutMS{100};")
    if config.drain_input:
        lines.append("int fDrainTimeoutMS{100};")
        lines.append("int fDrainMaxTimeoutCount{20};")
    if polled_channels(config):
        lines.append("fair::mq::PollerPtr fPoller;")
    return indent("\n".join(lines), 4)


def render_processing_declarations(config: GenerationConfig) -> str:
    lines: list[str] = []
    if config.processing_mode == "conditional-run":
        lines.append("auto ConditionalRun() -> bool override;")
    elif config.processing_mode == "run":
        lines.append("auto Run() -> void override;")
    elif config.processing_mode == "on-data":
        arg_type = "fair::mq::Parts& parts" if config.multipart_input else "fair::mq::MessagePtr& msg"
        lines.append(f"auto HandleData({arg_type}, int index) -> bool;")

    if config.processing_mode != "run" and config.output_channel:
        arg_type = "fair::mq::Parts& parts" if config.multipart_output else "fair::mq::MessagePtr& msg"
        lines.append(f"auto SendOutputMessage({arg_type}) -> bool;")
    if config.processing_mode != "run" and config.dqm_channel:
        arg_type = "fair::mq::Parts& parts" if config.multipart_dqm else "fair::mq::MessagePtr& msg"
        lines.append(f"auto SendDQMMessage({arg_type}) -> bool;")
    if config.drain_input:
        lines.append("auto PostRun() -> void override;")
        lines.append("auto DrainInputChannel() -> void;")
    if any((config.input_channel, config.output_channel, config.dqm_channel, needs_poll_timeout(config))):
        lines.append("auto InitTask() -> void override;")
    return indent("\n".join(lines), 4)


def render_custom_option_definitions(config: GenerationConfig) -> str:
    entries: list[str] = []
    if config.input_channel:
        entries.append(
            f'(opt::InputChannelName, bpo::value<std::string>()->default_value("{config.input_channel.default_name}"), "Name of input channel")'
        )
    if config.output_channel:
        entries.append(
            f'(opt::OutputChannelName, bpo::value<std::string>()->default_value("{config.output_channel.default_name}"), "Name of output channel")'
        )
    if config.dqm_channel:
        entries.append(
            f'(opt::DQMChannelName, bpo::value<std::string>()->default_value("{config.dqm_channel.default_name}"), "Name of DQM channel")'
        )
    if needs_poll_timeout(config):
        entries.append(
            '(opt::PollTimeoutMS, bpo::value<std::string>()->default_value("100"), "Poll timeout in milliseconds")'
        )
    if config.drain_input:
        entries.append(
            '(opt::DrainTimeoutMS, bpo::value<std::string>()->default_value("100"), "Input drain receive timeout in milliseconds; negative values are treated as 0")'
        )
        entries.append(
            '(opt::DrainMaxTimeoutCount, bpo::value<std::string>()->default_value("20"), "Maximum consecutive input drain timeouts before stopping; must be positive")'
        )

    if not entries:
        return "    (void)options;"

    lines = [f"    using opt = {qualified_class_name(config)}::OptionKey;", "    options.add_options()"]
    for index, entry in enumerate(entries):
        suffix = ";" if index == len(entries) - 1 else ""
        lines.append(f"        {entry}{suffix}")
    return "\n".join(lines)


def render_init_task_definition(config: GenerationConfig) -> str:
    if not any((config.input_channel, config.output_channel, config.dqm_channel, needs_poll_timeout(config))):
        return ""

    lines = [
        f"auto {config.class_name}::InitTask() -> void",
        "{",
        "    using opt = OptionKey;",
    ]
    if config.input_channel:
        lines.append("    fInputChannelName = fConfig->GetProperty<std::string>(opt::InputChannelName);")
    if config.output_channel:
        lines.append("    fOutputChannelName = fConfig->GetProperty<std::string>(opt::OutputChannelName);")
    if config.dqm_channel:
        lines.append("    fDQMChannelName = fConfig->GetProperty<std::string>(opt::DQMChannelName);")
    if needs_poll_timeout(config):
        lines.append("    fPollTimeoutMS = std::stoi(fConfig->GetProperty<std::string>(opt::PollTimeoutMS));")
    if config.drain_input:
        lines.append("    fDrainTimeoutMS = std::stoi(fConfig->GetProperty<std::string>(opt::DrainTimeoutMS));")
        lines.append("    if (fDrainTimeoutMS < 0) {")
        lines.append("        fDrainTimeoutMS = 0;")
        lines.append("    }")
        lines.append("    fDrainMaxTimeoutCount = std::stoi(fConfig->GetProperty<std::string>(opt::DrainMaxTimeoutCount));")
        lines.append("    if (fDrainMaxTimeoutCount <= 0) {")
        lines.append('        throw std::invalid_argument("drain-max-timeout-count must be positive");')
        lines.append("    }")

    if config.processing_mode == "on-data":
        lines.append("    OnData(fInputChannelName, &" + config.class_name + "::HandleData);")

    channels = polled_channels(config)
    if channels:
        args = ", ".join(f"f{channel.kind.upper() if channel.kind == 'dqm' else channel.kind.capitalize()}ChannelName" for channel in channels)
        args = args.replace("fDQMChannelName", "fDQMChannelName")
        lines.append(f"    fPoller = NewPoller({args});")

    lines.append("}")
    return "\n".join(lines)


def render_receive_block(config: GenerationConfig) -> str:
    if not config.input_channel:
        return ""
    message_type = "fair::mq::Parts parts;" if config.multipart_input else "auto msg = NewMessage();"
    variable = "parts" if config.multipart_input else "msg"
    receive_call = f"Receive({variable}, fInputChannelName)"
    if "input" in config.no_poll:
        receive_call = f"Receive({variable}, fInputChannelName, 0, fPollTimeoutMS)"
        return "\n".join(
            [
                message_type,
                f"if ({receive_call} >= 0) {{",
                '    LOG(info) << "received data on " << fInputChannelName;',
                "}",
            ]
        )
    return "\n".join(
        [
            "if (fPoller) {",
            "    fPoller->Poll(fPollTimeoutMS);",
            "}",
            "if (fPoller && fPoller->CheckInput(fInputChannelName, 0)) {",
            f"    {message_type}",
            f"    if ({receive_call} >= 0) {{",
            '        LOG(info) << "received data on " << fInputChannelName;',
            "    }",
            "}",
        ]
    )


def render_output_send_block(config: GenerationConfig) -> str:
    if not config.output_channel:
        return ""
    text = f"sample output from {config.class_name}"
    if config.multipart_output:
        return "\n".join(
            [
                "fair::mq::Parts outputParts;",
                f'outputParts.AddPart(NewSimpleMessage(std::string{{"{text}"}}));',
                "SendOutputMessage(outputParts);",
            ]
        )
    return "\n".join(
        [
            f'auto outputMsg = NewSimpleMessage(std::string{{"{text}"}});',
            "SendOutputMessage(outputMsg);",
        ]
    )


def render_dqm_send_block(config: GenerationConfig) -> str:
    if not config.dqm_channel:
        return ""
    text = f"sample DQM data from {config.class_name}"
    if config.multipart_dqm:
        return "\n".join(
            [
                "fair::mq::Parts dqmParts;",
                f'dqmParts.AddPart(NewSimpleMessage(std::string{{"{text}"}}));',
                "SendDQMMessage(dqmParts);",
            ]
        )
    return "\n".join(
        [
            f'auto dqmMsg = NewSimpleMessage(std::string{{"{text}"}});',
            "SendDQMMessage(dqmMsg);",
        ]
    )


def render_conditional_run_definition(config: GenerationConfig) -> str:
    if config.processing_mode != "conditional-run":
        return ""
    body_parts: list[str] = []
    receive_block = render_receive_block(config)
    if receive_block:
        body_parts.append(receive_block)
    output_send_block = render_output_send_block(config)
    if output_send_block:
        body_parts.append(output_send_block)
    dqm_send_block = render_dqm_send_block(config)
    if dqm_send_block:
        body_parts.append(dqm_send_block)
    body_parts.append("return true;")
    return f"""auto {config.class_name}::ConditionalRun() -> bool
{{
{indent(chr(10).join(body_parts), 4)}
}}"""


def render_run_definition(config: GenerationConfig) -> str:
    if config.processing_mode != "run":
        return ""
    return f"""auto {config.class_name}::Run() -> void
{{
}}"""


def render_on_data_definition(config: GenerationConfig) -> str:
    if config.processing_mode != "on-data":
        return ""
    arg_type = "fair::mq::Parts& parts" if config.multipart_input else "fair::mq::MessagePtr& msg"
    size_line = 'LOG(info) << "received multipart data on " << fInputChannelName << " index=" << index << " parts=" << parts.Size();' if config.multipart_input else 'LOG(info) << "received data on " << fInputChannelName << " index=" << index << " size=" << msg->GetSize();'
    body_parts = [size_line]
    output_send_block = render_output_send_block(config)
    if output_send_block:
        body_parts.append(output_send_block)
    dqm_send_block = render_dqm_send_block(config)
    if dqm_send_block:
        body_parts.append(dqm_send_block)
    body_parts.append("return true;")
    return f"""auto {config.class_name}::HandleData({arg_type}, int index) -> bool
{{
{indent(chr(10).join(body_parts), 4)}
}}"""


def render_send_output_definition(config: GenerationConfig) -> str:
    if not config.output_channel or config.processing_mode == "run":
        return ""
    arg_type = "fair::mq::Parts& parts" if config.multipart_output else "fair::mq::MessagePtr& msg"
    variable = "parts" if config.multipart_output else "msg"
    wait_block = ""
    if "output" not in config.no_poll:
        wait_block = """    while (!NewStatePending()) {
        if (fPoller) {
            fPoller->Poll(fPollTimeoutMS);
            if (fPoller->CheckOutput(fOutputChannelName, 0)) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(fPollTimeoutMS));
    }
    if (NewStatePending()) {
        LOG(debug) << "state transition requested before output channel became writable";
        return false;
    }
"""
    return f"""auto {config.class_name}::SendOutputMessage({arg_type}) -> bool
{{
{wait_block}    if (Send({variable}, fOutputChannelName) < 0) {{
        LOG(warn) << "failed to send data on " << fOutputChannelName;
        return false;
    }}
    return true;
}}"""


def render_send_dqm_definition(config: GenerationConfig) -> str:
    if not config.dqm_channel or config.processing_mode == "run":
        return ""
    arg_type = "fair::mq::Parts& parts" if config.multipart_dqm else "fair::mq::MessagePtr& msg"
    variable = "parts" if config.multipart_dqm else "msg"
    poll_block = ""
    if "dqm" not in config.no_poll:
        poll_block = """    if (fPoller) {
        fPoller->Poll(fPollTimeoutMS);
        if (!fPoller->CheckOutput(fDQMChannelName, 0)) {
            LOG(debug) << "DQM channel is not writable; dropping sample";
            return false;
        }
    }
"""
    return f"""auto {config.class_name}::SendDQMMessage({arg_type}) -> bool
{{
{poll_block}    if (Send({variable}, fDQMChannelName) < 0) {{
        LOG(warn) << "failed to send DQM data on " << fDQMChannelName;
        return false;
    }}
    return true;
}}"""


def render_post_run_definition(config: GenerationConfig) -> str:
    if not config.drain_input:
        return ""
    message_type = "fair::mq::Parts parts;" if config.multipart_input else "auto msg = NewMessage();"
    variable = "parts" if config.multipart_input else "msg"
    return f"""auto {config.class_name}::PostRun() -> void
{{
    DrainInputChannel();
}}

auto {config.class_name}::DrainInputChannel() -> void
{{
    int timeoutCount{{0}};
    while (timeoutCount < fDrainMaxTimeoutCount) {{
        {message_type}
        if (Receive({variable}, fInputChannelName, 0, fDrainTimeoutMS) <= 0) {{
            ++timeoutCount;
            continue;
        }}
        timeoutCount = 0;
        LOG(debug) << "drained data from " << fInputChannelName;
    }}
}}"""


def render_processing_definitions(config: GenerationConfig) -> str:
    parts = [
        render_init_task_definition(config),
        render_conditional_run_definition(config),
        render_run_definition(config),
        render_on_data_definition(config),
        render_send_output_definition(config),
        render_send_dqm_definition(config),
        render_post_run_definition(config),
    ]
    definitions = "\n\n".join(part for part in parts if part)
    if config.namespace_name is None or not definitions:
        return definitions
    return f"{namespace_open(config)}\n\n{definitions}\n\n{namespace_close(config)}"


def render_header_includes(config: GenerationConfig) -> str:
    includes = []
    if any((config.input_channel, config.output_channel, config.dqm_channel)):
        includes.append("#include <string>")
    if polled_channels(config):
        includes.append("#include <fairmq/Poller.h>")
    return "\n".join(includes)


def render_source_includes(config: GenerationConfig) -> str:
    includes = ["#include <memory>"]
    if config.processing_mode != "run" and any((config.output_channel, config.dqm_channel)):
        includes.extend(["#include <chrono>", "#include <thread>"])
    if needs_numeric_option(config) or (config.processing_mode != "run" and any((config.output_channel, config.dqm_channel))):
        includes.append("#include <string>")
    if config.multipart_input or config.multipart_output or config.multipart_dqm:
        includes.append("#include <fairmq/Parts.h>")
    if config.drain_input:
        includes.append("#include <stdexcept>")
    return "\n".join(includes)


def config_summary(config: GenerationConfig) -> str:
    channel_bits = []
    for spec in (config.input_channel, config.output_channel, config.dqm_channel):
        if spec:
            channel_bits.append(f"{spec.kind}={spec.option_key}:{spec.default_name}")
    if not channel_bits:
        channel_bits.append("channels=none")
    return (
        f"processing-mode={config.processing_mode}; "
        + ", ".join(channel_bits)
        + f"; multipart-input={str(config.multipart_input).lower()}; "
        + f"multipart-output={str(config.multipart_output).lower()}; "
        + f"multipart-dqm={str(config.multipart_dqm).lower()}; "
        + f"drain-input={str(config.drain_input).lower()}; "
        + f"namespace={config.namespace_name or 'none'}; "
        + f"cmake={str(config.generate_cmake).lower()}; "
        + f"readme={str(config.generate_readme).lower()}; "
        + "no-poll="
        + (",".join(sorted(config.no_poll)) if config.no_poll else "none")
    )


def render_cmake_generated_row(config: GenerationConfig) -> str:
    if not config.generate_cmake:
        return ""
    return "| `CMakeLists.txt.in` | `CMakeLists.txt` |\n"


def render_cmake_build_section(config: GenerationConfig) -> str:
    if not config.generate_cmake:
        return f"""## Build

`CMakeLists.txt` was not generated. Add `{config.class_name}.hpp` and `{config.class_name}.cpp` to
your existing build system and link the resulting executable with NestDAQ.

## Run

Use the NestDAQ helper script from the same install prefix used by your build.

```sh
<nestdaq-install-prefix>/scripts/start_device.sh <path-to-built-{config.class_name}>
```"""

    return f"""## Build

Configure this directory as an out-of-source CMake project. Set
`CMAKE_PREFIX_PATH` to the NestDAQ install prefix, and set
`CMAKE_INSTALL_PREFIX` to the install prefix for this generated device. These
prefixes may be the same directory. The generated CMake project uses C++17 by
default and rejects standards older than C++17.

```sh
cmake -S . -B ../build-{config.class_name} \\
  -DCMAKE_PREFIX_PATH=<nestdaq-install-prefix> \\
  -DCMAKE_INSTALL_PREFIX=<device-install-prefix>
cmake --build ../build-{config.class_name} --parallel
cmake --install ../build-{config.class_name}
```

The installed executable is placed under
`<device-install-prefix>/bin/{config.class_name}`.

## Run

Use the NestDAQ helper script from the same install prefix used for the build.

```sh
<nestdaq-install-prefix>/scripts/start_device.sh ./../build-{config.class_name}/{config.class_name}
<nestdaq-install-prefix>/scripts/start_device.sh <device-install-prefix>/bin/{config.class_name}
```"""


def render_template(template: str, substitutions: dict[str, str]) -> str:
    rendered = template
    for key, value in substitutions.items():
        rendered = rendered.replace(f"@{key}@", value)
    return rendered


def compact_cpp_blank_lines(text: str) -> str:
    while "\n\n\n" in text:
        text = text.replace("\n\n\n", "\n\n")
    return text


def render_substitutions(config: GenerationConfig) -> dict[str, str]:
    return {
        "CLASS_NAME": config.class_name,
        "QUALIFIED_CLASS_NAME": qualified_class_name(config),
        "HEADER_FILE": f"{config.class_name}.hpp",
        "SOURCE_FILE": f"{config.class_name}.cpp",
        "NAMESPACE_OPEN": namespace_open(config),
        "NAMESPACE_CLOSE": namespace_close(config),
        "HEADER_INCLUDES": render_header_includes(config),
        "SOURCE_INCLUDES": render_source_includes(config),
        "OPTION_KEY_DECLARATIONS": render_option_key_declarations(config),
        "MEMBER_DECLARATIONS": render_member_declarations(config),
        "PROCESSING_DECLARATIONS": render_processing_declarations(config),
        "CUSTOM_OPTION_DEFINITIONS": render_custom_option_definitions(config),
        "PROCESSING_DEFINITIONS": render_processing_definitions(config),
        "GENERATION_SUMMARY": config_summary(config),
        "CMAKE_GENERATED_ROW": render_cmake_generated_row(config),
        "CMAKE_BUILD_SECTION": render_cmake_build_section(config),
    }


def main() -> int:
    args = apply_interactive(parse_args())

    try:
        config = build_config(args)
        templates = selected_templates(config)
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    output_dir = args.output if args.output is not None else Path.cwd() / config.class_name
    output_dir = output_dir.expanduser()

    planned_files = [
        output_dir / template.output_pattern.format(class_name=config.class_name)
        for template in templates.values()
    ]

    if args.dry_run:
        print("Template source: built into generate-device-skeleton.py")
        print(f"Output directory: {output_dir}")
        print(f"Generation: {config_summary(config)}")
        for path in planned_files:
            print(f"would generate: {path}")
        return 0

    existing_files = [path for path in planned_files if path.exists()]
    if existing_files and not args.force:
        print("error: refusing to overwrite existing files:", file=sys.stderr)
        for path in existing_files:
            print(f"  {path}", file=sys.stderr)
        print("rerun with --force to overwrite them", file=sys.stderr)
        return 1

    output_dir.mkdir(parents=True, exist_ok=True)
    substitutions = render_substitutions(config)
    for template in templates.values():
        output_path = output_dir / template.output_pattern.format(class_name=config.class_name)
        rendered = render_template(template.text, substitutions)
        if output_path.suffix in (".cpp", ".hpp"):
            rendered = compact_cpp_blank_lines(rendered)
        output_path.write_text(rendered, encoding="utf-8")
        print(f"generated: {output_path}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
