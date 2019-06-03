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

package org.syslog_ng.elasticsearch_v2.client.http;

import io.searchbox.client.ESJestClientFactory;
import io.searchbox.client.JestClient;
import io.searchbox.client.JestClientFactory;
import io.searchbox.client.JestResult;
import io.searchbox.client.config.HttpClientConfig;
import io.searchbox.cluster.NodesInfo;
import org.apache.commons.lang3.StringUtils;
import org.apache.log4j.Logger;
import org.apache.log4j.PropertyConfigurator;
import org.syslog_ng.elasticsearch_v2.ElasticSearchOptions;
import org.syslog_ng.elasticsearch_v2.client.ESClient;
import org.syslog_ng.elasticsearch_v2.messageprocessor.ESMessageProcessorFactory;
import org.syslog_ng.elasticsearch_v2.messageprocessor.http.HttpMessageProcessor;
import org.syslog_ng.elasticsearch_v2.messageprocessor.http.IndexFieldHandler;

import java.io.File;
import java.io.IOException;
import java.util.function.Consumer;
import java.util.function.Function;

public class ESHttpClient implements ESClient {
	private ElasticSearchOptions options;
	private String authtype;
	private JestClient client;
	private HttpMessageProcessor messageProcessor;
	protected Logger logger;
	private Consumer<Integer> incDroppedBatchCounter;

  static  {
		final String log4jPath = System.getProperty("log4j.configuration");
		if (StringUtils.isNotBlank(log4jPath)) {
			final File f = new File(log4jPath);
			if (f.isFile()) {
				PropertyConfigurator.configure(f.getAbsolutePath());
			}
		}
  }


  public class HttpClientBuilderException extends RuntimeException {
    public HttpClientBuilderException() {}

    public HttpClientBuilderException(String msg) {
      super(msg);
    }

    public HttpClientBuilderException(String msg, Throwable cause) {
      super(msg, cause);
    }

    public HttpClientBuilderException(Throwable cause) {
      super(cause);
    }
  }

	public ESHttpClient(ElasticSearchOptions options) {
		this.options = options;
		logger = Logger.getRootLogger();
		messageProcessor = ESMessageProcessorFactory.getMessageProcessor(options, this);
	}

  protected void setupHttpClientBuilder(HttpClientConfig.Builder httpClientConfigBuilder, ElasticSearchOptions options) {}

  private HttpClientConfig buildHttpClientConfig() {
		int concurrentRequests = options.getConcurrentRequests();
		int totalRoutes = options.getClusterUrls().size();
		String connPoolTimes = System.getProperty("HttpConnectionPoolTimes", "3");
 		HttpClientConfig.Builder httpClientConfigBuilder = new HttpClientConfig
			.Builder(options.getClusterUrls())
			.multiThreaded(true)
						.defaultMaxTotalConnectionPerRoute(concurrentRequests)
						.maxTotalConnection(concurrentRequests * Integer.valueOf(connPoolTimes))
			.defaultSchemeForDiscoveredNodes(options.getClientMode());

		/* HTTP Basic authentication requested */
		if (options.getHttpAuthType().equals("basic")) {
			httpClientConfigBuilder.defaultCredentials(options.getHttpAuthTypeBasicUsername(), options.getHttpAuthTypeBasicPassword());
    }
    httpClientConfigBuilder.readTimeout(30000);
    setupHttpClientBuilder(httpClientConfigBuilder, this.options);

    return httpClientConfigBuilder.build();
  }

	private JestClient createClient() {
    ESJestClientFactory clientFactory = new ESJestClientFactory();
	  clientFactory.setHttpClientConfig(buildHttpClientConfig());
		return clientFactory.getObject();
	}

	public JestClient getClient() {
		return client;
	}

	public synchronized void incDroppedBatchCounter(int batchSize) {
  	if(incDroppedBatchCounter != null)
  		this.incDroppedBatchCounter.accept(batchSize);
  }

	@Override
	public boolean open() {
		if (client == null) {
      try {
			  client = createClient();
      }
      catch (ESHttpClient.HttpClientBuilderException e) {
        logger.error(e.getMessage());
        return false;
      }
		}
		messageProcessor.init();
		return true;
	}

	@Override
	public void close() {
		messageProcessor.flush();
		messageProcessor.deinit();
	}

	@Override
	public boolean isOpened() {
		return true;
	}

	@Override
	public void init(Consumer<Integer> incDroppedBatchCounter) {
		this.client = createClient();
		this.incDroppedBatchCounter = incDroppedBatchCounter;
	}

	@Override
	public void deinit() {
		client.shutdownClient();
		options.deinit();
	}

	@Override
	public boolean send(Function<IndexFieldHandler, Object> messageBuilder) {
		return messageProcessor.send(messageBuilder);
	}

	@Override
	public String getClusterName() {
		NodesInfo nodesinfo = new NodesInfo.Builder().build();
		String clusterName = options.getCluster();
		try {
			JestResult result = client.execute(nodesinfo);
			if (result != null ) {
				Object cname = result.getValue("cluster_name");
				if(cname != null) {
					clusterName = result.getValue("cluster_name").toString();
				} else {
					logger.info("Failed to get cluster name from the client, use the name set in the config file: " + clusterName);
				}
			}
		} catch (IOException e) {
      logger.info("Failed to get cluster name from the client, use the name set in the config file: " + clusterName);
		}
		return clusterName;
	}

	@Override
	public boolean flush() {
		return messageProcessor.flush();
	}
}
