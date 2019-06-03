/*
 * Copyright (c) 2016 Balabit
 * Copyright (c) 2016 Laszlo Budai <laszlo.budai@balabit.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

package io.searchbox.client;

import com.google.gson.Gson;
import io.searchbox.client.config.HttpClientConfig;
import io.searchbox.client.config.discovery.NodeChecker;
import io.searchbox.client.config.idle.HttpReapableConnectionManager;
import io.searchbox.client.config.idle.IdleConnectionReaper;
import io.searchbox.client.http.ESJestHttpClient;
import org.apache.http.conn.HttpClientConnectionManager;
import org.apache.http.impl.client.CloseableHttpClient;
import org.apache.http.impl.client.HttpClients;
import org.apache.http.impl.nio.client.CloseableHttpAsyncClient;
import org.apache.http.impl.nio.client.HttpAsyncClients;
import org.apache.http.nio.conn.NHttpClientConnectionManager;

public class ESJestClientFactory extends  JestClientFactory {

  private HttpClientConfig httpClientConfig;

  public JestClient getObject() {
    ESJestHttpClient client = new ESJestHttpClient();

    if (httpClientConfig == null) {
      log.debug("There is no configuration to create http client. Going to create simple client with default values");
      httpClientConfig = new HttpClientConfig.Builder("http://localhost:9200").build();
    }

    client.setRequestCompressionEnabled(httpClientConfig.isRequestCompressionEnabled());
    client.setServers(httpClientConfig.getServerList());
    final HttpClientConnectionManager connectionManager = getConnectionManager();
    final NHttpClientConnectionManager asyncConnectionManager = getAsyncConnectionManager();
    client.setHttpClient(createHttpClient(connectionManager));
    client.setAsyncClient(createAsyncHttpClient(asyncConnectionManager));

    // set custom gson instance
    Gson gson = httpClientConfig.getGson();
    if (gson == null) {
      log.info("Using default GSON instance");
    } else {
      log.info("Using custom GSON instance");
      client.setGson(gson);
    }

    // set discovery (should be set after setting the httpClient on jestClient)
    if (httpClientConfig.isDiscoveryEnabled()) {
      log.info("Node Discovery enabled...");
      NodeChecker nodeChecker = new NodeChecker(client, httpClientConfig);
      client.setNodeChecker(nodeChecker);
      nodeChecker.startAsync();
      nodeChecker.awaitRunning();
    } else {
      log.info("Node Discovery disabled...");
    }

    // schedule idle connection reaping if configured
    if (httpClientConfig.getMaxConnectionIdleTime() > 0) {
      log.info("Idle connection reaping enabled...");

      IdleConnectionReaper reaper = new IdleConnectionReaper(httpClientConfig, new HttpReapableConnectionManager(connectionManager, asyncConnectionManager));
      client.setIdleConnectionReaper(reaper);
      reaper.startAsync();
      reaper.awaitRunning();
    } else {
      log.info("Idle connection reaping disabled...");
    }

    return client;
  }

  protected CloseableHttpClient createHttpClient(HttpClientConnectionManager connectionManager) {
    return configureHttpClient(
            HttpClients.custom()
                    .setConnectionManager(connectionManager)
                    .setDefaultRequestConfig(getRequestConfig())
                    .setProxyAuthenticationStrategy(httpClientConfig.getProxyAuthenticationStrategy())
                    .setRoutePlanner(getRoutePlanner())
                    .setDefaultCredentialsProvider(httpClientConfig.getCredentialsProvider())
    ).build();
  }

  protected CloseableHttpAsyncClient createAsyncHttpClient(NHttpClientConnectionManager connectionManager) {
    return configureHttpClient(
            HttpAsyncClients.custom()
                    .setConnectionManager(connectionManager)
                    .setDefaultRequestConfig(getRequestConfig())
                    .setProxyAuthenticationStrategy(httpClientConfig.getProxyAuthenticationStrategy())
                    .setRoutePlanner(getRoutePlanner())
                    .setDefaultCredentialsProvider(httpClientConfig.getCredentialsProvider())
    ).build();
  }

  public void setHttpClientConfig(HttpClientConfig httpClientConfig) {
    this.httpClientConfig = httpClientConfig;
    super.setHttpClientConfig(httpClientConfig);
  }

}