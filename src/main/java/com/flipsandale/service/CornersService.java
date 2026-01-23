package com.flipsandale.service;

import com.flipsandale.dto.MazeRequest;
import com.flipsandale.dto.MazeResponse;
import java.awt.*;
import java.awt.image.BufferedImage;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.util.Base64;
import javax.imageio.ImageIO;
import org.springframework.http.HttpStatusCode;
import org.springframework.stereotype.Service;
import org.springframework.web.reactive.function.client.WebClient;
import org.springframework.web.reactive.function.client.WebClientResponseException;
import reactor.core.publisher.Mono;

@Service
public class CornersService {
  private final WebClient webClient;
  private static final int CELL_SIZE = 20; // Size of each cell in pixels
  private static final Color WALL_COLOR = Color.BLACK;
  private static final Color EMPTY_COLOR = Color.WHITE;

  public CornersService(WebClient.Builder webClientBuilder) {
    this.webClient =
        webClientBuilder.baseUrl("https://corners-app-9d22c3fdfd0c.herokuapp.com").build();
  }

  public MazeResponse createMaze() {
    MazeRequest request = new MazeRequest("sidewinder", 20, 20, 42, true);
    return createMaze(request);
  }

  public MazeResponse createMaze(MazeRequest request) {
    try {
      return webClient
          .post()
          .uri("/api/mazes/create")
          .bodyValue(request == null ? new MazeRequest() : request)
          .retrieve()
          .onStatus(
              HttpStatusCode::isError,
              clientResponse ->
                  clientResponse
                      .bodyToMono(String.class)
                      .flatMap(
                          errorBody -> Mono.error(new RuntimeException("API error: " + errorBody))))
          .bodyToMono(MazeResponse.class)
          .block();
    } catch (WebClientResponseException e) {
      throw new RuntimeException("WebClient error: " + e.getResponseBodyAsString(), e);
    } catch (Exception e) {
      throw new RuntimeException("Unknown error: " + e.getMessage(), e);
    }
  }

  /** Decodes the base64 encoded maze data */
  public String decodeBase64Data(String base64Data) {
    try {
      byte[] decodedBytes = Base64.getDecoder().decode(base64Data);
      return new String(decodedBytes);
    } catch (Exception e) {
      throw new RuntimeException("Failed to decode base64 data: " + e.getMessage(), e);
    }
  }

  /**
   * Creates a PNG image from the ASCII maze representation
   *
   * @param asciiMaze The decoded ASCII maze string
   * @return byte array of the PNG image
   */
  public byte[] createMazeImage(String asciiMaze) throws IOException {
    String[] lines = asciiMaze.split("\n");
    if (lines.length == 0) {
      throw new IllegalArgumentException("Empty maze data");
    }

    // Calculate image dimensions based on ASCII maze size
    int height = lines.length;
    int width = lines[0].length();

    // Find the maximum distance value for gradient calculation
    int maxDistance = findMaxDistance(asciiMaze);

    // Create buffered image
    BufferedImage image =
        new BufferedImage(width * CELL_SIZE, height * CELL_SIZE, BufferedImage.TYPE_INT_RGB);
    Graphics2D g2d = image.createGraphics();

    // Enable anti-aliasing for better quality
    g2d.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);

    // Parse cell distance values for each line with distance values (non-wall lines)
    for (int y = 0; y < height; y++) {
      String line = y < lines.length ? lines[y] : "";

      // First pass: identify distance values and their cells
      Integer[] cellDistances = new Integer[width];

      // Scan the line to find distance values and propagate them to surrounding spaces
      for (int x = 0; x < line.length(); x++) {
        char c = line.charAt(x);
        int distance = parseBase36(c);

        if (distance >= 0) {
          // Found a distance value - mark this position and look back for spaces
          cellDistances[x] = distance;

          // Look backward for spaces that belong to this cell
          for (int lookBack = x - 1; lookBack >= 0; lookBack--) {
            char prevChar = line.charAt(lookBack);
            if (prevChar == ' ') {
              cellDistances[lookBack] = distance;
            } else if (prevChar == '|') {
              // Hit a wall, stop looking back
              break;
            } else {
              // Hit another distance value or wall character
              break;
            }
          }
        }
      }

      // Second pass: draw the pixels
      for (int x = 0; x < width && x < line.length(); x++) {
        char c = line.charAt(x);
        Color color;

        // Check if this position has a cell distance assigned
        if (cellDistances[x] != null) {
          // Use the cell's distance for gradient color
          color = getGradientColor(cellDistances[x], maxDistance);
        } else {
          // Use the character-based color (walls, etc.)
          color = determineColor(c, maxDistance);
        }

        // Draw the pixel
        g2d.setColor(color);
        g2d.fillRect(x * CELL_SIZE, y * CELL_SIZE, CELL_SIZE, CELL_SIZE);
      }
    }

    g2d.dispose();

    // Convert to byte array
    ByteArrayOutputStream baos = new ByteArrayOutputStream();
    ImageIO.write(image, "PNG", baos);
    return baos.toByteArray();
  }

  /** Finds the maximum distance value in the maze for gradient calculation */
  private int findMaxDistance(String asciiMaze) {
    int maxDistance = 0;
    for (char c : asciiMaze.toCharArray()) {
      if (Character.isLetterOrDigit(c) && c != '+' && c != '-' && c != '|') {
        int distance = parseBase36(c);
        if (distance > maxDistance) {
          maxDistance = distance;
        }
      }
    }
    return maxDistance;
  }

  /** Parses a base36 character to its integer value */
  private int parseBase36(char c) {
    if (c >= '0' && c <= '9') {
      return c - '0';
    } else if (c >= 'A' && c <= 'Z') {
      return 10 + (c - 'A');
    } else if (c >= 'a' && c <= 'z') {
      return 10 + (c - 'a');
    }
    return -1;
  }

  /** Gets gradient color for a distance value Creates a gradient from blue (start) to red (end) */
  private Color getGradientColor(int distance, int maxDistance) {
    if (maxDistance == 0) {
      return EMPTY_COLOR;
    }

    float ratio = (float) distance / maxDistance;

    // Create gradient: Blue -> Cyan -> Green -> Yellow -> Red
    int red, green, blue;

    if (ratio < 0.25f) {
      // Blue to Cyan
      float localRatio = ratio / 0.25f;
      red = 0;
      green = (int) (255 * localRatio);
      blue = 255;
    } else if (ratio < 0.5f) {
      // Cyan to Green
      float localRatio = (ratio - 0.25f) / 0.25f;
      red = 0;
      green = 255;
      blue = (int) (255 * (1 - localRatio));
    } else if (ratio < 0.75f) {
      // Green to Yellow
      float localRatio = (ratio - 0.5f) / 0.25f;
      red = (int) (255 * localRatio);
      green = 255;
      blue = 0;
    } else {
      // Yellow to Red
      float localRatio = (ratio - 0.75f) / 0.25f;
      red = 255;
      green = (int) (255 * (1 - localRatio));
      blue = 0;
    }

    return new Color(red, green, blue);
  }

  /**
   * Determines the color for a given character in the maze Walls (+, -, |) are black, spaces are
   * white
   */
  private Color determineColor(char c, int maxDistance) {
    // Wall characters
    if (c == '+' || c == '-' || c == '|') {
      return WALL_COLOR;
    }

    // Empty space
    if (c == ' ') {
      return EMPTY_COLOR;
    }

    // Default to white for any unrecognized character
    return EMPTY_COLOR;
  }
}
