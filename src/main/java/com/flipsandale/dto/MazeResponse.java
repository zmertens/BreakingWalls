package com.flipsandale.dto;

import java.util.Map;

public class MazeResponse {
  private String data;
  private String createdAt;
  private String version_str;
  private Map<String, Object> config;

  public MazeResponse() {}

  // Getters and setters
  public String getData() {
    return data;
  }

  public void setData(String data) {
    this.data = data;
  }

  public String getCreatedAt() {
    return createdAt;
  }

  public void setCreatedAt(String createdAt) {
    this.createdAt = createdAt;
  }

  public String getVersion_str() {
    return version_str;
  }

  public void setVersion_str(String version_str) {
    this.version_str = version_str;
  }

  public Map<String, Object> getConfig() {
    return config;
  }

  public void setConfig(Map<String, Object> config) {
    this.config = config;
  }
}
