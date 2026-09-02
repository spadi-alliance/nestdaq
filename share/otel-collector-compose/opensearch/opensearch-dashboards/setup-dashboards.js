#!/usr/bin/env node
'use strict';

const http = require('http');
const https = require('https');

const dashboardsUrl = new URL(process.env.OPENSEARCH_DASHBOARDS_URL || 'http://opensearch-dashboards:5601');
const timeoutMs = Number.parseInt(process.env.OPENSEARCH_DASHBOARDS_SETUP_TIMEOUT_MS || '120000', 10);
const intervalMs = Number.parseInt(process.env.OPENSEARCH_DASHBOARDS_SETUP_INTERVAL_MS || '2000', 10);

const dataViews = [
  {id: 'otel-logs', title: 'otel-logs-*', timeFieldName: '@timestamp'},
  {id: 'otel-traces', title: 'otel-traces-*', timeFieldName: '@timestamp'},
];

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function request(method, path, body) {
  const url = new URL(path, dashboardsUrl);
  const payload = body === undefined ? undefined : JSON.stringify(body);
  const transport = url.protocol === 'https:' ? https : http;

  const options = {
    method,
    hostname: url.hostname,
    port: url.port || (url.protocol === 'https:' ? 443 : 80),
    path: `${url.pathname}${url.search}`,
    headers: {
      'Accept': 'application/json',
      'Content-Type': 'application/json',
      'osd-xsrf': 'true',
      'kbn-xsrf': 'true',
    },
  };

  if (payload !== undefined) {
    options.headers['Content-Length'] = Buffer.byteLength(payload);
  }

  return new Promise((resolve, reject) => {
    const req = transport.request(options, (res) => {
      let responseBody = '';
      res.setEncoding('utf8');
      res.on('data', (chunk) => {
        responseBody += chunk;
      });
      res.on('end', () => {
        let json;
        try {
          json = responseBody.length === 0 ? undefined : JSON.parse(responseBody);
        } catch (_) {
          json = undefined;
        }
        resolve({statusCode: res.statusCode, body: responseBody, json});
      });
    });

    req.on('error', reject);
    if (payload !== undefined) {
      req.write(payload);
    }
    req.end();
  });
}

function fail(message, response) {
  if (response !== undefined) {
    throw new Error(`${message}: status=${response.statusCode}, body=${response.body}`);
  }
  throw new Error(message);
}

async function waitForDashboards() {
  const deadline = Date.now() + timeoutMs;
  let lastError = 'not ready';

  while (Date.now() < deadline) {
    try {
      const response = await request('GET', '/api/status');
      if (response.statusCode >= 200 && response.statusCode < 300) {
        console.log(`OpenSearch Dashboards is ready at ${dashboardsUrl.toString()}`);
        return;
      }
      lastError = `status=${response.statusCode}, body=${response.body}`;
    } catch (error) {
      lastError = error.message;
    }
    await sleep(intervalMs);
  }

  fail(`Timed out waiting for OpenSearch Dashboards (${lastError})`);
}

async function savedObjectExists(type, id) {
  const response = await request('GET', `/api/saved_objects/${encodeURIComponent(type)}/${encodeURIComponent(id)}`);
  if (response.statusCode === 200) {
    return true;
  }
  if (response.statusCode === 404) {
    return false;
  }
  fail(`Failed to check saved object ${type}/${id}`, response);
}

async function createDataViewIfMissing(view) {
  if (await savedObjectExists('index-pattern', view.id)) {
    console.log(`Data view ${view.title} already exists; leaving it unchanged`);
    return;
  }

  const response = await request(
    'POST',
    `/api/saved_objects/index-pattern/${encodeURIComponent(view.id)}`,
    {attributes: {title: view.title, timeFieldName: view.timeFieldName}}
  );

  if (response.statusCode >= 200 && response.statusCode < 300) {
    console.log(`Created data view ${view.title}`);
    return;
  }
  if (response.statusCode === 409) {
    console.log(`Data view ${view.title} was created concurrently; leaving it unchanged`);
    return;
  }
  fail(`Failed to create data view ${view.title}`, response);
}

function extractDefaultIndex(response) {
  const settings = response.json && response.json.settings;
  const defaultIndex = settings && settings.defaultIndex;
  if (defaultIndex === undefined || defaultIndex === null) {
    return undefined;
  }
  if (typeof defaultIndex === 'string') {
    return defaultIndex;
  }
  if (typeof defaultIndex.userValue === 'string') {
    return defaultIndex.userValue;
  }
  if (typeof defaultIndex.value === 'string') {
    return defaultIndex.value;
  }
  return undefined;
}

async function getDefaultIndex() {
  const endpoints = ['/api/opensearch-dashboards/settings', '/api/kibana/settings'];
  let lastResponse;

  for (const endpoint of endpoints) {
    const response = await request('GET', endpoint);
    if (response.statusCode >= 200 && response.statusCode < 300) {
      return {endpoint, value: extractDefaultIndex(response)};
    }
    if (response.statusCode !== 404) {
      lastResponse = response;
    }
  }

  fail('Failed to read OpenSearch Dashboards advanced settings', lastResponse);
}

async function setDefaultIndexIfMissing(indexPatternId) {
  const current = await getDefaultIndex();
  if (current.value !== undefined && current.value !== '') {
    console.log(`Default data view is already set to ${current.value}; leaving it unchanged`);
    return;
  }

  const endpointPrefix = current.endpoint.replace(/\/settings$/, '');
  const response = await request('POST', `${endpointPrefix}/settings/defaultIndex`, {value: indexPatternId});
  if (response.statusCode >= 200 && response.statusCode < 300) {
    console.log(`Set default data view to ${indexPatternId}`);
    return;
  }
  fail(`Failed to set default data view to ${indexPatternId}`, response);
}

async function main() {
  await waitForDashboards();
  for (const view of dataViews) {
    await createDataViewIfMissing(view);
  }
  await setDefaultIndexIfMissing('otel-logs');
}

main().catch((error) => {
  console.error(error.stack || error.message);
  process.exitCode = 1;
});
