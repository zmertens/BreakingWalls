package com.flipsandale.game;

import com.jme3.asset.AssetManager;
import com.jme3.light.AmbientLight;
import com.jme3.light.DirectionalLight;
import com.jme3.light.PointLight;
import com.jme3.material.Material;
import com.jme3.math.ColorRGBA;
import com.jme3.math.Vector3f;
import com.jme3.renderer.ViewPort;
import com.jme3.scene.Node;
import com.jme3.shadow.DirectionalLightShadowRenderer;
import com.jme3.texture.Texture;

/**
 * Manages all materials, lights, and shadows for the game.
 *
 * <p>Note: This is NOT a Spring bean because it requires AssetManager, which is only available in
 * the JMonkeyEngine SimpleApplication context. Instantiate directly in MazeGameApp.
 *
 * <p>Provides: - Material creation for different object types (platforms, player, skybox) -
 * Lighting setup (directional sun light, ambient light, optional point lights) - Shadow rendering
 * for depth and realism
 */
public class MaterialManager {

  private final AssetManager assetManager;
  private Texture wallDiffuseTexture;

  public MaterialManager(AssetManager assetManager) {
    this.assetManager = assetManager;
  }

  /**
   * Creates a lit platform material with customizable color. Uses Phong lighting model for
   * realistic surface reflection.
   *
   * @param baseColor The RGB color of the platform
   * @return A Material configured for Phong lighting
   */
  public Material createLitPlatformMaterial(ColorRGBA baseColor) {
    Material mat = new Material(assetManager, "Common/MatDefs/Light/Lighting.j3md");
    mat.setBoolean("UseMaterialColors", true);

    // Use base color directly (already dark, will show lighting well)
    mat.setColor("Diffuse", baseColor);

    // Set ambient color (color in shadow) - much darker for dramatic shadow contrast
    ColorRGBA ambientColor =
        new ColorRGBA(baseColor.r * 0.2f, baseColor.g * 0.2f, baseColor.b * 0.2f, baseColor.a);
    mat.setColor("Ambient", ambientColor);

    // Set specular (shiny reflections) - white for realistic highlights
    mat.setColor("Specular", ColorRGBA.White);

    // Shininess (1 = rough, 128 = very smooth) - higher for more visible highlights
    mat.setFloat("Shininess", 64f);

    return mat;
  }

  /**
   * Creates a metallic platform material with enhanced reflectivity.
   *
   * @param baseColor The base metallic color
   * @return A highly reflective Material
   */
  public Material createMetallicMaterial(ColorRGBA baseColor) {
    Material mat = createLitPlatformMaterial(baseColor);
    // Increase shininess for metallic surfaces
    mat.setFloat("Shininess", 96f);
    // Make specular highlights very bright
    mat.setColor("Specular", ColorRGBA.White);
    return mat;
  }

  /**
   * Creates a matte platform material with no specular highlights.
   *
   * @param baseColor The matte color
   * @return A non-reflective Material
   */
  public Material createMatteMaterial(ColorRGBA baseColor) {
    Material mat = createLitPlatformMaterial(baseColor);
    // Disable shiny reflections
    mat.setColor("Specular", ColorRGBA.Black);
    return mat;
  }

  /**
   * Creates a glowing material for special platform effects.
   *
   * @param baseColor The color of the platform
   * @param glowColor The color of the glow effect
   * @return A Material with glow effect
   */
  public Material createGlowingMaterial(ColorRGBA baseColor, ColorRGBA glowColor) {
    Material mat = createLitPlatformMaterial(baseColor);
    mat.setColor("GlowColor", glowColor);
    return mat;
  }

  /**
   * Creates an unshaded material (useful for UI elements or skybox).
   *
   * @param color The color of the material
   * @return An unshaded Material
   */
  public Material createUnshadedMaterial(ColorRGBA color) {
    Material mat = new Material(assetManager, "Common/MatDefs/Misc/Unshaded.j3md");
    mat.setColor("Color", color);
    return mat;
  }

  /**
   * Returns a lit, textured material suitable for blocks/walls. Uses a brick texture from
   * resources/static and applies basic ambient/specular settings. The wallId seeds a tiny color
   * tint for variety while keeping the texture visible.
   */
  public Material getTexturedWallMaterial(long wallId) {
    if (wallDiffuseTexture == null) {
      wallDiffuseTexture = assetManager.loadTexture("static/brick_brown.png");
      wallDiffuseTexture.setWrap(Texture.WrapMode.Repeat);
    }

    Material mat = new Material(assetManager, "Common/MatDefs/Light/Lighting.j3md");
    mat.setTexture("DiffuseMap", wallDiffuseTexture);
    mat.setBoolean("UseMaterialColors", true);
    mat.setColor("Diffuse", ColorRGBA.White);
    // Slight ambient so unlit faces are still visible
    mat.setColor("Ambient", new ColorRGBA(0.4f, 0.4f, 0.4f, 1f));
    mat.setColor("Specular", new ColorRGBA(0.2f, 0.2f, 0.2f, 1f));
    mat.setFloat("Shininess", 12f);

    // Tiny per-wall tint for variation without overpowering the texture
    java.util.Random rnd = new java.util.Random(wallId);
    float tint = 0.05f * (rnd.nextFloat() - 0.5f); // ±2.5%
    mat.setColor("Diffuse", new ColorRGBA(1f + tint, 1f + tint, 1f + tint, 1f));
    return mat;
  }

  /**
   * Sets up comprehensive lighting for the scene. Includes: - Directional light (sun) from above -
   * Ambient light for shadows - Optional point lights for accent lighting
   *
   * @param rootNode The root scene node to attach lights to
   */
  public void setupLighting(Node rootNode) {
    // Strong ambient light to provide base color
    AmbientLight ambientLight = new AmbientLight(new ColorRGBA(0.4f, 0.4f, 0.4f, 1.0f));
    rootNode.addLight(ambientLight);

    // Very bright directional light from camera perspective (above and to the side)
    // This light should be obvious and make a big difference
    DirectionalLight mainLight = new DirectionalLight();
    // Light coming from above-left toward bottom-right
    mainLight.setDirection(new Vector3f(1.0f, -1.0f, 1.0f).normalizeLocal());
    // EXTREME brightness to make lighting very obvious
    mainLight.setColor(new ColorRGBA(2.0f, 2.0f, 2.0f, 1.0f));
    rootNode.addLight(mainLight);

    // Additional bright point light near a platform location for clear shadows
    PointLight brightPointLight = new PointLight(new Vector3f(20f, 25f, 20f));
    brightPointLight.setColor(new ColorRGBA(1.5f, 1.5f, 1.5f, 1.0f));
    brightPointLight.setRadius(100f);
    rootNode.addLight(brightPointLight);

    System.out.println(
        "✓ DRAMATIC lighting setup: Extreme directional light (2.0x brightness) + bright point light");
  }

  /**
   * Enables shadow rendering for the scene. Creates realistic shadows from the main directional
   * light.
   *
   * @param viewPort The viewport to add shadow processor to
   * @param rootNode The root scene node
   * @param shadowMapSize Size of shadow map texture (e.g., 1024, 2048)
   */
  public void enableShadows(ViewPort viewPort, Node rootNode, int shadowMapSize) {
    // Find the directional light (sun)
    DirectionalLight sunLight = null;
    for (int i = 0; i < rootNode.getLocalLightList().size(); i++) {
      if (rootNode.getLocalLightList().get(i) instanceof DirectionalLight) {
        sunLight = (DirectionalLight) rootNode.getLocalLightList().get(i);
        break;
      }
    }

    if (sunLight == null) {
      // Create a directional light if none exists
      sunLight = new DirectionalLight();
      sunLight.setDirection(new Vector3f(-0.5f, -1.0f, -0.5f).normalizeLocal());
      sunLight.setColor(new ColorRGBA(2.0f, 2.0f, 2.0f, 1.0f));
      rootNode.addLight(sunLight);
      System.out.println("⚠️ Created fallback directional light for shadows");
    }

    // Create shadow renderer with extreme settings for visibility
    DirectionalLightShadowRenderer shadowRenderer =
        new DirectionalLightShadowRenderer(assetManager, shadowMapSize, 4);
    shadowRenderer.setLight(sunLight);
    // Increase shadow intensity to 1.0 for maximum darkness in shadows
    shadowRenderer.setShadowIntensity(1.0f);
    shadowRenderer.setEdgeFilteringMode(com.jme3.shadow.EdgeFilteringMode.PCF4);

    // Add to viewport
    viewPort.addProcessor(shadowRenderer);
    System.out.println("✓ Shadow rendering enabled with maximum intensity");
  }

  /**
   * Gets a varied material from a preset palette based on platform ID. Automatically selects
   * between lit, metallic, and matte finishes.
   *
   * @param platformId ID of the platform (for consistent coloring)
   * @return A varied Material with good visual properties
   */
  public Material getPaletteMaterial(long platformId) {
    java.util.Random colorGen = new java.util.Random(platformId);

    // MUCH DARKER color palette for obvious lighting effects
    // Dark saturated colors show lighting and shadows much more dramatically
    ColorRGBA[] colorPalette = {
      new ColorRGBA(0.0f, 0.4f, 0.5f, 1.0f), // Dark Cyan
      new ColorRGBA(0.0f, 0.5f, 0.0f, 1.0f), // Dark Green
      new ColorRGBA(0.6f, 0.3f, 0.0f, 1.0f), // Dark Orange
      new ColorRGBA(0.5f, 0.0f, 0.5f, 1.0f), // Dark Magenta
      new ColorRGBA(0.0f, 0.3f, 0.6f, 1.0f), // Dark Blue
      new ColorRGBA(0.6f, 0.6f, 0.0f, 1.0f), // Dark Yellow
      new ColorRGBA(0.0f, 0.4f, 0.3f, 1.0f), // Dark Teal
      new ColorRGBA(0.6f, 0.0f, 0.0f, 1.0f), // Dark Red
      new ColorRGBA(0.3f, 0.5f, 0.0f, 1.0f), // Dark Lime
      new ColorRGBA(0.3f, 0.0f, 0.6f, 1.0f), // Dark Purple
    };

    ColorRGBA baseColor = colorPalette[(int) (platformId % colorPalette.length)];

    // Slightly vary the color
    float variation = 1.0f + (colorGen.nextFloat() - 0.5f) * 0.1f;
    ColorRGBA variedColor =
        new ColorRGBA(
            Math.max(0.0f, Math.min(1.0f, baseColor.r * variation)),
            Math.max(0.0f, Math.min(1.0f, baseColor.g * variation)),
            Math.max(0.0f, Math.min(1.0f, baseColor.b * variation)),
            baseColor.a);

    // Randomly select material type based on platformId
    int materialType = (int) (platformId % 3);
    switch (materialType) {
      case 0:
        return createLitPlatformMaterial(variedColor);
      case 1:
        return createMetallicMaterial(variedColor);
      case 2:
        return createMatteMaterial(variedColor);
      default:
        return createLitPlatformMaterial(variedColor);
    }
  }
}
