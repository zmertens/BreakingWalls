package com.flipsandale.controller;

import com.flipsandale.dto.MazeRequest;
import com.flipsandale.dto.MazeResponse;
import com.flipsandale.service.CornersService;
import java.util.Map;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.http.HttpHeaders;
import org.springframework.http.HttpStatus;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RestController;

@RestController
public class LevelController {
  private final CornersService CornersService;

  @Autowired
  public LevelController(CornersService CornersService) {
    this.CornersService = CornersService;
  }

  @GetMapping("/api/mazes/create")
  public Object createMaze() {
    try {
      MazeResponse response = CornersService.createMaze();
      return response;
    } catch (Exception e) {
      return "Maze API error: " + e.getMessage();
    }
  }

  @GetMapping(value = "/api/mazes/image", produces = MediaType.IMAGE_PNG_VALUE)
  public ResponseEntity<byte[]> createMazeImage() {
    try {
      // Get maze data from API
      MazeResponse response = CornersService.createMaze();

      // Decode base64 data
      String asciiMaze = CornersService.decodeBase64Data(response.getData());

      // Create PNG image
      byte[] imageBytes = CornersService.createMazeImage(asciiMaze);

      // Return image with proper headers
      HttpHeaders headers = new HttpHeaders();
      headers.setContentType(MediaType.IMAGE_PNG);
      headers.setContentLength(imageBytes.length);
      headers.set("Content-Disposition", "inline; filename=\"maze.png\"");

      return new ResponseEntity<>(imageBytes, headers, HttpStatus.OK);
    } catch (Exception e) {
      return ResponseEntity.status(HttpStatus.INTERNAL_SERVER_ERROR)
          .body(("Error creating maze image: " + e.getMessage()).getBytes());
    }
  }

  @PostMapping("/api/mazes/create")
  public Object createMazeWithParams(@RequestBody MazeRequest request) {
    try {
      MazeResponse response = CornersService.createMaze(request);
      return response;
    } catch (Exception e) {
      return Map.of("error", "Maze API error: " + e.getMessage());
    }
  }
}
