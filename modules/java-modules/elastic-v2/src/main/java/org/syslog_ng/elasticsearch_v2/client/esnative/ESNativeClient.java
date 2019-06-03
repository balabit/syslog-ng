/*
 * Copyright (c) 2016 Balabit
 * Copyright (c) 2016 Viktor Juhasz <viktor.juhasz@balabit.com>
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

package org.syslog_ng.elasticsearch_v2.client.esnative;

import java.io.File;
import java.net.URI;
import java.nio.file.Paths;
import java.util.function.Consumer;
import java.util.function.Function;

import org.apache.log4j.Logger;
import org.elasticsearch.ElasticsearchException;
import org.elasticsearch.action.admin.cluster.health.ClusterHealthRequestBuilder;
import org.elasticsearch.action.admin.cluster.health.ClusterHealthResponse;
import org.elasticsearch.client.Client;
import org.elasticsearch.cluster.health.ClusterHealthStatus;
import org.elasticsearch.common.settings.SettingsException;
import org.elasticsearch.common.settings.Settings.Builder;
import org.syslog_ng.elasticsearch_v2.ElasticSearchOptions;
import org.syslog_ng.elasticsearch_v2.client.ESClient;
import org.syslog_ng.elasticsearch_v2.messageprocessor.ESIndex;
import org.syslog_ng.elasticsearch_v2.messageprocessor.ESMessageProcessorFactory;
import org.syslog_ng.elasticsearch_v2.messageprocessor.esnative.ESNativeMessageProcessor;
import org.syslog_ng.elasticsearch_v2.messageprocessor.http.IndexFieldHandler;

public abstract class ESNativeClient implements ESClient {
	private Client client;
	protected ESNativeMessageProcessor messageProcessor;
	private static final String TIMEOUT = "5s";
	protected Logger logger;
	protected ElasticSearchOptions options;

	public ESNativeClient(ElasticSearchOptions options) {
		this.options = options;
		logger = Logger.getRootLogger();
		messageProcessor = ESMessageProcessorFactory.getMessageProcessor(options, this);
	}

	private boolean waitForStatus(ClusterHealthStatus status) {
		ClusterHealthRequestBuilder healthRequest = client.admin().cluster().prepareHealth();
		healthRequest.setTimeout(TIMEOUT);
		healthRequest.setWaitForStatus(status);
		ClusterHealthResponse response = (ClusterHealthResponse) healthRequest.execute().actionGet();
		return !response.isTimedOut();
	}

	protected void connect() throws ElasticsearchException {
		String clusterName = getClusterName();
		if (options.getSkipClusterHealthCheck()) {
		        logger.warn("Skip checking cluster state, cluster_name='" + clusterName + '"');
		        return;
		}
		logger.info("Checking cluster state..., cluster_name='" + clusterName + "'");
		if (!waitForStatus(ClusterHealthStatus.GREEN)) {
			logger.debug("Failed to wait for green");
			logger.debug("Wait for read yellow status...");

			if (!waitForStatus(ClusterHealthStatus.YELLOW)) {
				logger.debug("Timedout");
				throw new ElasticsearchException("Can't connect to cluster: " + clusterName);
			}
		}
		logger.info("Cluster is ready to work, cluster_name='" + clusterName + "'");
	}

	public final boolean open() {
		if (client == null) {
			client = createClient();
		}
		try {
			connect();
		}
		catch (ElasticsearchException e) {
			logger.error("Failed to connect to " + getClusterName() + ", reason='" + e.getMessage() + "'");
			close();
			return false;
		}
        messageProcessor.init();
		return true;
	}

	@Override
	public String getClusterName() {
		return client.settings().get("cluster.name");
	}

	public final void close() {
		messageProcessor.flush();
		messageProcessor.deinit();
		client.close();
		resetClient();
	}

	public abstract boolean isOpened();

	public abstract void deinit();

	public final void init(Consumer<Integer> incDroppedBatchCounter) {
		client = createClient();
	}

	public abstract Client createClient();

	public Client getClient() {
		return client;
	}

	private void resetClient() {
		this.client = null;
	}

    protected void loadConfigFile(String cfgFile, Builder settingsBuilder) {
        if (cfgFile == null || cfgFile.isEmpty()) {
            return;
        }
        try {
            URI url = new File(cfgFile).toURI();
            settingsBuilder.loadFromPath(Paths.get(url));
        } catch (SettingsException e) {
            logger.warn("Can't load settings from file, file = '" + cfgFile + "', reason = '" + e.getMessage() + "'");
        }
    }

	@Override
	public boolean send(Function<IndexFieldHandler, Object> messageBuilder) {
		return messageProcessor.send(messageBuilder);
	}

	@Override
	public boolean flush() {
		return true;
	}
}
