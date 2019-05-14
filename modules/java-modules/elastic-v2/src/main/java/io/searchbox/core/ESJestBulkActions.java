package io.searchbox.core;

import com.google.gson.Gson;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import io.searchbox.action.Action;
import io.searchbox.client.JestResult;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.UnsupportedEncodingException;
import java.net.URLEncoder;
import java.util.Map;
import java.util.regex.Pattern;

public class ESJestBulkActions implements Action<BulkResult> {

  public static String CHARSET = "utf-8";
  final static Logger log = LoggerFactory.getLogger(ESJestBulkActions.class);

  private StringBuilder data = new StringBuilder();

  private final String indexName;
  private final String typeName;
  private final String pipeline;
  private final boolean debugEnabled;

  private final String uri;

  private static final String NEWLINE = "\n";
  private static final String idxMetaPlaceHolder = "{\"index\" : {} }" + NEWLINE;

  private static final String BEGIN_INDEX = "{\"index\" : {";
  private static final String END_INDEX = "} }" + NEWLINE;
  private static final String QUOTE = "\"";
  private static final String DELIMETER = "\" : \"";
  private static final String COMMA = ",";
  public static final String BLANK = "";

  public ESJestBulkActions(String defaultIndex, String defaultType, String defaultPipeline,
                           boolean debugEnabled) {
//    super(new Bulk.Builder().defaultIndex(defaultIndex).defaultType(defaultType));
    this.indexName = defaultIndex;
    this.typeName = defaultType;
    this.debugEnabled = debugEnabled;
    if (StringUtils.isNotBlank(defaultPipeline)) {
      this.pipeline = defaultPipeline;
    } else {
      this.pipeline = BLANK;
    }
    uri = buildURI();
  }

  private String buildURI() {
    StringBuilder sb = new StringBuilder();

    try {
      if (StringUtils.isNotBlank(indexName)) {
        sb.append(URLEncoder.encode(indexName, CHARSET));

        if (StringUtils.isNotBlank(typeName)) {
          sb.append("/").append(URLEncoder.encode(typeName, CHARSET));
        }
      }
    } catch (UnsupportedEncodingException e) {
      // unless CHARSET is overridden with a wrong value in a subclass,
      // this exception won't be thrown.
      log.error("Error occurred while adding index/type to uri", e);
    }

    return sb.append("/_bulk").toString();
  }


  private void addIndexMetadata(final String tag, final String value, final boolean isFirst) {
    if (StringUtils.isBlank(value)) {
      return;
    }
    if(!isFirst) {
      data.append(COMMA);
    }
    data.append(QUOTE).append(tag).append(DELIMETER).append(value).append(QUOTE);
  }

  public final void addMessage(String msg, String index, String type, String pipeline, String id) {
    if(this.indexName.equalsIgnoreCase(index) &&
            this.typeName.equalsIgnoreCase(type) &&
            this.pipeline.equalsIgnoreCase(StringUtils.isBlank(pipeline) ? BLANK : pipeline)) {
      data.append(idxMetaPlaceHolder);
    } else {
      // { "index" : { "_index" : "<>", "_type" : "<>", "_id" : "<>", "pipeline" : "<>" } }
      data.append(BEGIN_INDEX);
      addIndexMetadata("_index", index, true);
      addIndexMetadata("_type", type, false);
      addIndexMetadata("pipeline", pipeline,false);
      addIndexMetadata("_id", id, false);
      data.append(END_INDEX);
    }
    data.append(msg).append(NEWLINE);
  }

  //{"took":0,"ingest_took":0,"errors":false,"items":[{
  //{"took":0,"errors":false,"items":[{
  private static final Pattern successPattern =
          Pattern.compile("\\{\"took\":[0-9]+,(\"ingest_took\":[0-9]+,)?\"errors\":false,");

  @Override
  public String getURI() {
    return uri;
  }

  @Override
  public String getRestMethodName() {
    return "POST";
  }

  @Override
  public String getData(Gson gson) {
    data.append(NEWLINE);
    String payload = data.toString();
    data = null;
    if (debugEnabled)
        log.warn("SB: Http Bulk Payload : " + payload);
    return payload;
  }

  @Override
  public String getPathToResult() {
    return null;
  }

  @Override
  public Map<String, Object> getHeaders() {
    return null;
  }

  @Override
  public BulkResult createNewElasticSearchResult(String responseBody, int statusCode, String reasonPhrase, Gson gson) {
    return createNewElasticSearchResult(new BulkResult(gson), responseBody, statusCode, reasonPhrase, gson);
  }

  protected boolean isHttpSuccessful(int httpCode) {
    return (httpCode / 100) == 2;
  }

  protected JsonObject parseResponseBody(String responseBody) {
    if (responseBody != null && !responseBody.trim().isEmpty()) {
      return new JsonParser().parse(responseBody).getAsJsonObject();
    }
    return new JsonObject();
  }

  protected BulkResult createNewElasticSearchResult(BulkResult result, String responseBody, int statusCode, String reasonPhrase, Gson gson) {
    JsonObject jsonMap = parseResponseBody(responseBody);
    result.setResponseCode(statusCode);
    result.setJsonString(responseBody);
    result.setJsonObject(jsonMap);
    result.setPathToResult(getPathToResult());

    if (isHttpSuccessful(statusCode)) {
      if(jsonMap.has("errors") && jsonMap.get("errors").getAsBoolean())
      {
        result.setSucceeded(false);
        result.setErrorMessage("One or more of the items in the Bulk request failed, check BulkResult.getItems() for more information.");
        if (debugEnabled)
            log.info("Bulk operation failed due to one or more failed actions within the Bulk request");
      } else {
        result.setSucceeded(true);
      }
    } else {
      result.setSucceeded(false);
      // provide the generic HTTP status code error, if one hasn't already come in via the JSON response...
      // eg.
      //  IndicesExist will return 404 (with no content at all) for a missing index, but:
      //  Update will return 404 (with an error message for DocumentMissingException)
      if (result.getErrorMessage() == null) {
        result.setErrorMessage(statusCode + " " + (reasonPhrase == null ? "null" : reasonPhrase));
      }
      if (debugEnabled)
          log.info("Bulk operation failed with an HTTP error");
    }
    return result;
  }

  public boolean isSuccessFull(String firstFewLines, int statusCode) {
    if (!isHttpSuccessful(statusCode)) {
      return false;
    }

    return successPattern.matcher(firstFewLines).find(0);
  }
}
