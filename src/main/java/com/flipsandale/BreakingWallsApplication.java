package com.flipsandale;

import com.flipsandale.game.GameLauncher;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.ConfigurableApplicationContext;

@SpringBootApplication
public class BreakingWallsApplication {

  public static void main(String[] args) {
    // Start Spring Boot (non-blocking, keeps REST API available)
    ConfigurableApplicationContext context =
        new SpringApplicationBuilder(BreakingWallsApplication.class).headless(false).run(args);

    // Launch the game window automatically
    GameLauncher gameLauncher = context.getBean(GameLauncher.class);
    gameLauncher.launchGame();
  }
}
