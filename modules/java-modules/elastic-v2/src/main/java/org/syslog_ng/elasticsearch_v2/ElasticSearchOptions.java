/*
 * Copyright (c) 2016 Balabit
 * Copyright (c) 2016 Viktor Juhasz <viktor.juhasz@balabit.com>
 * Copyright (c) 2016 Viktor Tusa <tusavik@gmail.com>
 * Copyright (c) 2016 Laszlo Budai <laszlo.budai@balabit.com>
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

package org.syslog_ng.elasticsearch_v2;

import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Set;

import org.syslog_ng.LogDestination;
import org.syslog_ng.options.*;

public class ElasticSearchOptions {

	public static String IDENTIFIER = "identifier";
	public static String SERVER = "server";
	public static String PORT = "port";
	public static String CLUSTER = "cluster";
	public static String INDEX = "index";
	public static String TYPE = "type";
	public static String MESSAGE_TEMPLATE = "message_template";
	public static String CUSTOM_ID = "custom_id";
	public static String FLUSH_LIMIT = "flush_limit";
	public static String FLUSH_TIMEOUT = "flush_timeout";
	public static String CLIENT_MODE = "client_mode";
	public static String CONFIG_FILE = "resource";
	public static String CONCURRENT_REQUESTS = "concurrent_requests";
	public static String CLUSTER_URL = "cluster_url";
	public static String PIPELINE = "pipeline";

	public static String SERVER_DEFAULT = "localhost";
	private static final String PORT_DEFAULT = "0";
	public static String MESSAGE_TEMPLATE_DEFAULT = "$(format-json --scope rfc5424 --exclude DATE --key ISODATE)";
	public static String FLUSH_LIMIT_DEFAULT = "5000";
	public static String FLUSH_TIMEOUT_DEFAULT = "0";
	public static String CLIENT_MODE_TRANSPORT = "transport";
	public static String CLIENT_MODE_NODE = "node";
	public static String CLIENT_MODE_SEARCHGUARD = "searchguard";
	public static String CLIENT_MODE_HTTP = "http";
	public static String CLIENT_MODE_HTTPS = "https";
	public static String SKIP_CLUSTER_HEALTH_CHECK = "skip_cluster_health_check";
	public static String SKIP_CLUSTER_HEALTH_CHECK_DEFAULT = "false";
	public static String JAVA_KEYSTORE_FILEPATH = "java_keystore_filepath";
	public static String JAVA_KEYSTORE_PASSWORD = "java_keystore_password";
	public static String JAVA_TRUSTSTORE_FILEPATH = "java_truststore_filepath";
	public static String JAVA_TRUSTSTORE_PASSWORD = "java_truststore_password";
	public static String HTTP_AUTH_TYPE = "http_auth_type";
	public static String HTTP_AUTH_TYPE_NONE = "none";
	public static String HTTP_AUTH_TYPE_BASIC = "basic";
	public static String HTTP_AUTH_TYPE_CLIENTCERT = "clientcert";
	public static String HTTP_AUTH_TYPE_BASIC_USERNAME = "http_auth_type_basic_username";
	public static String HTTP_AUTH_TYPE_BASIC_PASSWORD = "http_auth_type_basic_password";
	public static HashSet<String> CLIENT_MODES  = new HashSet<String>(Arrays.asList(CLIENT_MODE_TRANSPORT, CLIENT_MODE_NODE, CLIENT_MODE_HTTP, CLIENT_MODE_HTTPS, CLIENT_MODE_SEARCHGUARD));
	public static HashSet<String> HTTP_AUTH_TYPES  = new HashSet<String>(Arrays.asList(HTTP_AUTH_TYPE_NONE, HTTP_AUTH_TYPE_BASIC, HTTP_AUTH_TYPE_CLIENTCERT));
	public static final HashMap<String, Integer> DEFAULT_PORTS_BY_MODE = new HashMap<String, Integer>() {
		{
			this.put(CLIENT_MODE_HTTP, 9200);
			this.put(CLIENT_MODE_HTTPS, 9200);
			this.put(CLIENT_MODE_NODE, 9300);
			this.put(CLIENT_MODE_TRANSPORT, 9300);
			this.put(CLIENT_MODE_SEARCHGUARD, 9300);
		}
	};

	public static String CLIENT_MODE_DEFAULT = CLIENT_MODE_TRANSPORT;
	public static String HTTP_AUTH_TYPE_DEFAULT = HTTP_AUTH_TYPE_NONE;
	public static String CONCURRENT_REQUESTS_DEFAULT = "1";

	private LogDestination owner;
	private Options options;

	public ElasticSearchOptions(LogDestination owner) {
		this.owner = owner;
		options = new Options();
		fillOptions();
	}

	public void init() throws InvalidOptionException {
		options.validate();
	}

	public void deinit() {
		options.deinit();
	}

	public Option getOption(String optionName) {
		return options.get(optionName);
	}

	public TemplateOption getIdentifier() { return options.getTemplateOption(IDENTIFIER); }

	public TemplateOption getMessageTemplate() {
		return options.getTemplateOption(MESSAGE_TEMPLATE);
	}

	public TemplateOption getCustomId() {
		return options.getTemplateOption(CUSTOM_ID);
	}

	public TemplateOption getIndex() {
		return options.getTemplateOption(INDEX);
	}

	public TemplateOption getPipeline() { return options.getTemplateOption(PIPELINE);	}

	public TemplateOption getType() {
		return options.getTemplateOption(TYPE);
	}

	public String[] getServerList() {
		return options.get(SERVER).getValueAsStringList(" ");
	}

	public int getPort() {
		Integer port;

		if (options.get(PORT).getValue().equals(PORT_DEFAULT))
			port = DEFAULT_PORTS_BY_MODE.get(getClientMode());
		else
			port = options.get(PORT).getValueAsInteger();

		return port;
	}

	public String getCluster() {
		return options.get(CLUSTER).getValue();
	}

	public int getFlushLimit() {
		return options.get(FLUSH_LIMIT).getValueAsInteger();
	}

	public int getFlushTimeout() {
		return options.get(FLUSH_TIMEOUT).getValueAsInteger();
	}

	public String getClientMode() {
		return options.get(CLIENT_MODE).getValue();
	}

	public String getHttpAuthType() {
		return options.get(HTTP_AUTH_TYPE).getValue();
	}

	public String getConfigFile() {
		return options.get(CONFIG_FILE).getValue();
	}

        public int getConcurrentRequests() {
        	return options.get(CONCURRENT_REQUESTS).getValueAsInteger();
        }

        public boolean getSkipClusterHealthCheck() {
                return options.get(SKIP_CLUSTER_HEALTH_CHECK).getValueAsBoolean();
        }

	public String getJavaKeyStoreFilepath() {
		return options.get(JAVA_KEYSTORE_FILEPATH).getValue();
	}

	public String getJavaKeyStorePassword() {
		return options.get(JAVA_KEYSTORE_PASSWORD).getValue();
	}

	public String getJavaTrustStoreFilepath() {
		return options.get(JAVA_TRUSTSTORE_FILEPATH).getValue();
	}

	public String getJavaTrustStorePassword() {
		return options.get(JAVA_TRUSTSTORE_PASSWORD).getValue();
	}

	public String getHttpAuthTypeBasicUsername() {
		return options.get(HTTP_AUTH_TYPE_BASIC_USERNAME).getValue();
	}

	public String getHttpAuthTypeBasicPassword() {
		return options.get(HTTP_AUTH_TYPE_BASIC_PASSWORD).getValue();
	}

	public Set<String> getClusterUrls() {
		String[] cluster_url = options.get(CLUSTER_URL).getValueAsStringList(" ");
		if (cluster_url[0].isEmpty()) {
			String[] server_list = getServerList();
			cluster_url = new String[server_list.length];
			for (int i = 0; i < server_list.length; i++) {
				StringBuilder url = new StringBuilder();
				url.append(options.get(CLIENT_MODE).getValue());
				url.append("://");
				url.append(server_list[i]);
				url.append(":");
				url.append(getPort());
				cluster_url[i] = url.toString();
			}
		}
		return new HashSet<String>(Arrays.asList(cluster_url));
	}


	private void fillOptions() {
		fillStringOptions();
		fillTemplateOptions();
		fillBooleanOptions();
	}

	private void fillTemplateOptions() {
		options.put(new TemplateOption(owner.getConfigHandle(), new RequiredOptionDecorator(new StringOption(owner, INDEX))));
		options.put(new TemplateOption(owner.getConfigHandle(), new RequiredOptionDecorator(new StringOption(owner, TYPE))));
		options.put(new TemplateOption(owner.getConfigHandle(), new StringOption(owner, PIPELINE)));
		options.put(new TemplateOption(owner.getConfigHandle(), new StringOption(owner, MESSAGE_TEMPLATE, MESSAGE_TEMPLATE_DEFAULT)));
		options.put(new TemplateOption(owner.getConfigHandle(), new StringOption(owner, CUSTOM_ID)));
		options.put(new TemplateOption(owner.getConfigHandle(), new StringOption(owner, IDENTIFIER)));
	}

	private void fillStringOptions() {
		options.put(new StringOption(owner, SERVER, SERVER_DEFAULT));
		options.put(new PortCheckDecorator(new StringOption(owner, PORT, PORT_DEFAULT)));
		options.put(new StringOption(owner, CLUSTER));
		options.put(new IntegerRangeCheckOptionDecorator(new StringOption(owner, FLUSH_LIMIT, FLUSH_LIMIT_DEFAULT), -1, Integer.MAX_VALUE));
		options.put(new IntegerRangeCheckOptionDecorator(new StringOption(owner, FLUSH_TIMEOUT, FLUSH_TIMEOUT_DEFAULT), -1, Integer.MAX_VALUE));
		options.put(new EnumOptionDecorator(new StringOption(owner, CLIENT_MODE, CLIENT_MODE_DEFAULT), CLIENT_MODES));
		options.put(new EnumOptionDecorator(new StringOption(owner, HTTP_AUTH_TYPE, HTTP_AUTH_TYPE_DEFAULT), HTTP_AUTH_TYPES));
		options.put(new StringOption(owner, CONFIG_FILE));
		options.put(new IntegerRangeCheckOptionDecorator(new StringOption(owner, CONCURRENT_REQUESTS, CONCURRENT_REQUESTS_DEFAULT), 0, Integer.MAX_VALUE));
		options.put(new StringOption(owner, CLUSTER_URL, ""));
		options.put(new StringOption(owner, JAVA_KEYSTORE_FILEPATH, ""));
		options.put(new StringOption(owner, JAVA_KEYSTORE_PASSWORD, ""));
		options.put(new StringOption(owner, JAVA_TRUSTSTORE_FILEPATH, ""));
		options.put(new StringOption(owner, JAVA_TRUSTSTORE_PASSWORD, ""));
		options.put(new StringOption(owner, HTTP_AUTH_TYPE_BASIC_USERNAME, ""));
		options.put(new StringOption(owner, HTTP_AUTH_TYPE_BASIC_PASSWORD, ""));
	}

	private void fillBooleanOptions() {
	        options.put(new BooleanOptionDecorator(new StringOption(owner, SKIP_CLUSTER_HEALTH_CHECK, SKIP_CLUSTER_HEALTH_CHECK_DEFAULT)));
	}
}
