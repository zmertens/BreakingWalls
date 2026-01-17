package com.flipsandale.dto;

public class MazeRequest {
  private String algo;
  private int rows;
  private int columns;
  private int seed;
  private boolean distances;

  public MazeRequest() {}

  public MazeRequest(String algo, int rows, int columns, int seed, boolean distances) {
    this.algo = algo;
    this.rows = rows;
    this.columns = columns;
    this.seed = seed;
    this.distances = distances;
  }

  public String getAlgo() {
    return algo;
  }

  public void setAlgo(String algo) {
    this.algo = algo;
  }

  public int getRows() {
    return rows;
  }

  public void setRows(int rows) {
    this.rows = rows;
  }

  public int getColumns() {
    return columns;
  }

  public void setColumns(int columns) {
    this.columns = columns;
  }

  public int getSeed() {
    return seed;
  }

  public void setSeed(int seed) {
    this.seed = seed;
  }

  public boolean isDistances() {
    return distances;
  }

  public void setDistances(boolean distances) {
    this.distances = distances;
  }
}
