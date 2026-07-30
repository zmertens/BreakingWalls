import * as Phaser from 'phaser'
import { runMazeBuilder } from '../../mazebuilder/runtime'

type PathNode = {
  x: number
  y: number
  value: number
  revealed: boolean
}

const NODES_PER_CHUNK = 18
const CHUNK_WIDTH = 860
const CHUNK_GAP = 70
const START_X = 180
const START_Y = 260
const HOVER_RADIUS = 52
const NODE_JITTER_X = 54
const NODE_JITTER_Y = 56
const MAGNET_RADIUS = 170
const MAGNET_MAX_STRENGTH = 0.68
const GLOW_PULSE_SPEED = 0.0065
const GLOW_PULSE_AMPLITUDE = 0.18
const GENERATE_SOUND_THRESHOLD = 0.75

const fragmentSource = `
precision mediump float;

uniform float uTime;
uniform vec2 uResolution;

void main() {
    vec2 uv = gl_FragCoord.xy / uResolution.xy;
    float wave = sin((uv.x * 5.0) + (uTime * 0.2)) * 0.05;
    float drift = cos((uv.y * 8.0) - (uTime * 0.15)) * 0.04;

    vec3 deep = vec3(0.05, 0.08, 0.14);
    vec3 mid = vec3(0.08, 0.22, 0.30);
    vec3 glow = vec3(0.20, 0.44, 0.34);

    float blendA = smoothstep(0.0, 1.0, uv.y + wave + drift);
    float blendB = smoothstep(0.0, 1.0, uv.x + wave);
    vec3 color = mix(deep, mid, blendA);
    color = mix(color, glow, blendB * 0.35);

    gl_FragColor = vec4(color, 1.0);
}
`

export class Game extends Phaser.Scene {
  private camera!: Phaser.Cameras.Scene2D.Camera
  private shaderLayer!: Phaser.GameObjects.Shader
  private parallaxFar!: Phaser.GameObjects.TileSprite
  private parallaxNear!: Phaser.GameObjects.TileSprite
  private mazeBackdrop!: Phaser.GameObjects.TileSprite
  private gridGraphics!: Phaser.GameObjects.Graphics
  private hiddenPathGraphics!: Phaser.GameObjects.Graphics
  private revealedPathGraphics!: Phaser.GameObjects.Graphics
  private nodeGraphics!: Phaser.GameObjects.Graphics
  private pointerGlow!: Phaser.GameObjects.Graphics
  private cursor!: Phaser.Physics.Arcade.Sprite
  private instructionText!: Phaser.GameObjects.Text
  private droneLights: Phaser.Physics.Arcade.Image[] = []
  private statusText!: Phaser.GameObjects.Text

  private pathNodes: PathNode[] = []
  private hoveredNodeIndex = -1
  private chunkCounter = 0
  private maxNodeValue = -1
  private furthestLitX = START_X
  private chunkInFlight = false

  private targetWorldX = START_X
  private targetWorldY = START_Y
  private nextTrailEmitAt = 0
  private lastTrailX = START_X
  private lastTrailY = START_Y

  private ambienceSound?: Phaser.Sound.BaseSound
  private stepSound?: Phaser.Sound.BaseSound
  private chunkSound?: Phaser.Sound.BaseSound
  private generateSound?: Phaser.Sound.BaseSound
  private cursorFacing: 'up' | 'down' | 'left' | 'right' = 'down'

  constructor() {
    super('Game')
  }

  create() {
    this.camera = this.cameras.main
    this.camera.setBackgroundColor(0x081420)
    this.camera.setBounds(0, 0, 120000, this.scale.height)

    this.shaderLayer = new Phaser.GameObjects.Shader(
      this,
      {
        name: 'calm-waves',
        fragmentSource,
        setupUniforms: (setUniform: (name: string, value: unknown) => void) => {
          setUniform('uTime', this.time.now * 0.001)
          setUniform('uResolution', [this.scale.width, this.scale.height])
        },
      },
      this.scale.width * 0.5,
      this.scale.height * 0.5,
      this.scale.width,
      this.scale.height
    )
    this.add.existing(this.shaderLayer)
    this.shaderLayer.setScrollFactor(0).setDepth(0)

    this.parallaxFar = this.add
      .tileSprite(0, 0, this.scale.width, this.scale.height, 'maze-bg')
      .setOrigin(0)
      .setScrollFactor(0)
      .setAlpha(0.08)
      .setTint(0x8fd4ff)
      .setDepth(1)

    this.parallaxNear = this.add
      .tileSprite(0, 0, this.scale.width, this.scale.height, 'maze-bg')
      .setOrigin(0)
      .setScrollFactor(0)
      .setAlpha(0.12)
      .setTint(0x8fffc3)
      .setDepth(2)

    this.mazeBackdrop = this.add
      .tileSprite(0, 0, this.scale.width, this.scale.height, 'maze-bg')
      .setOrigin(0)
      .setScrollFactor(0)
      .setAlpha(0.1)
      .setDepth(3)

    this.gridGraphics = this.add.graphics()
    this.hiddenPathGraphics = this.add.graphics()
    this.revealedPathGraphics = this.add.graphics()
    this.nodeGraphics = this.add.graphics()
    this.pointerGlow = this.add.graphics()
    this.gridGraphics.setDepth(9)
    this.hiddenPathGraphics.setDepth(10)
    this.revealedPathGraphics.setDepth(11)
    this.nodeGraphics.setDepth(12)
    this.pointerGlow.setDepth(18)

    this.cursor = this.physics.add.sprite(START_X, START_Y, 'characters', 0)
    this.cursor.setAlpha(0.85)
    this.cursor.setDepth(20)
    this.cursor.play('character-idle-down')
    const cursorBody = this.cursor.body as Phaser.Physics.Arcade.Body | null
    if (cursorBody) {
      cursorBody.allowGravity = false
    }
    this.cursor.setDamping(true)
    this.cursor.setDrag(0.08)
    this.cursor.setMaxVelocity(300, 300)

    this.instructionText = this.add
      .text(18, 16, 'Trace the trail, relax and flow', {
        fontFamily: 'Verdana',
        fontSize: 22,
        color: '#e7f9ff',
        stroke: '#11263a',
        strokeThickness: 4,
      })
      .setScrollFactor(0)
      .setDepth(30)

    this.statusText = this.add
      .text(18, 102, 'Warming maze builder...', {
        fontFamily: 'Verdana',
        fontSize: 18,
        color: '#9ce9c1',
        stroke: '#0c1f24',
        strokeThickness: 3,
      })
      .setScrollFactor(0)
      .setDepth(30)

    this.setupAudio()
    this.setupAmbientPhysics()
    this.setupInput()
    this.layoutHud(this.scale.height)
    this.scale.on('resize', this.handleResize, this)

    this.events.once('shutdown', this.shutdown, this)

    void this.seedInitialPath()
  }

  private handleResize(gameSize: Phaser.Structs.Size): void {
    const { width, height } = gameSize
    this.camera.setBounds(0, 0, 120000, height)
    this.physics.world.setBounds(0, 0, 120000, height)

    this.shaderLayer.setPosition(width * 0.5, height * 0.5)
    this.shaderLayer.setSize(width, height)
    this.shaderLayer.setDisplaySize(width, height)

    this.parallaxFar.setSize(width, height)
    this.parallaxNear.setSize(width, height)
    this.mazeBackdrop.setSize(width, height)
    this.layoutHud(height)
  }

  private layoutHud(height: number): void {
    const uiScale = Phaser.Math.Clamp(height / 768, 0.72, 1.35)
    this.cursor.setScale(0.24 * uiScale)

    this.instructionText.setPosition(18, 16)
    this.instructionText.setFontSize(Math.round(22 * uiScale))
    this.instructionText.setStroke(
      '#11263a',
      Math.max(2, Math.round(4 * uiScale))
    )

    this.statusText.setPosition(23, height - (18 + 18 * uiScale))
    this.statusText.setFontSize(Math.round(18 * uiScale))
    this.statusText.setStroke('#0c1f24', Math.max(2, Math.round(3 * uiScale)))
  }

  update(): void {
    this.parallaxFar.tilePositionX = this.camera.scrollX * 0.02
    this.parallaxFar.tilePositionY = this.time.now * 0.003
    this.parallaxNear.tilePositionX = this.camera.scrollX * 0.05
    this.parallaxNear.tilePositionY = this.time.now * 0.006
    this.mazeBackdrop.tilePositionX = this.camera.scrollX * 0.09
    this.mazeBackdrop.tilePositionY = this.time.now * 0.008

    const pointer = this.input.activePointer
    const magneticTarget = this.getMagneticTarget(
      pointer.worldX,
      pointer.worldY
    )
    this.targetWorldX = Phaser.Math.Linear(
      this.targetWorldX,
      magneticTarget.x,
      0.24
    )
    this.targetWorldY = Phaser.Math.Linear(
      this.targetWorldY,
      magneticTarget.y,
      0.24
    )
    this.emitPointerTrail(pointer.worldX, pointer.worldY)
    this.updateHoverLighting(pointer.worldX, pointer.worldY)
    this.drawPointerGlow(this.targetWorldX, this.targetWorldY)

    const dx = this.targetWorldX - this.cursor.x
    const dy = this.targetWorldY - this.cursor.y
    const body = this.cursor.body as Phaser.Physics.Arcade.Body | null
    if (body) {
      body.velocity.x += dx * 0.08
      body.velocity.y += dy * 0.08
      this.updateCursorAnimation(body.velocity.x, body.velocity.y)
    }

    const followX = Math.max(0, this.cursor.x - this.scale.width * 0.35)
    this.camera.scrollX = Phaser.Math.Linear(this.camera.scrollX, followX, 0.05)

    if (!this.chunkInFlight && this.pathNodes.length > 0) {
      const tailX = this.pathNodes[this.pathNodes.length - 1].x
      if (tailX - this.furthestLitX < this.scale.width * 1.25) {
        this.chunkInFlight = true
        void this.appendChunk().finally(() => {
          this.chunkInFlight = false
        })
      }
    }

    this.instructionText.setAlpha(0.85 + Math.sin(this.time.now * 0.004) * 0.15);
  }

  private getMagneticTarget(
    pointerWorldX: number,
    pointerWorldY: number
  ): { x: number; y: number } {
    let nearestNode: PathNode | null = null
    let nearestDistance = MAGNET_RADIUS

    for (const node of this.pathNodes) {
      const distance = Phaser.Math.Distance.Between(
        pointerWorldX,
        pointerWorldY,
        node.x,
        node.y
      )
      if (distance < nearestDistance) {
        nearestDistance = distance
        nearestNode = node
      }
    }

    if (!nearestNode) {
      return { x: pointerWorldX, y: pointerWorldY }
    }

    const normalized = Phaser.Math.Clamp(
      1 - nearestDistance / MAGNET_RADIUS,
      0,
      1
    )
    const magneticStrength = normalized * normalized * MAGNET_MAX_STRENGTH

    return {
      x: Phaser.Math.Linear(pointerWorldX, nearestNode.x, magneticStrength),
      y: Phaser.Math.Linear(pointerWorldY, nearestNode.y, magneticStrength),
    }
  }

  private drawPointerGlow(x: number, y: number): void {
    this.pointerGlow.clear()

    const pulse = (Math.sin(this.time.now * GLOW_PULSE_SPEED) + 1) * 0.5
    const pulseScale = 1 + (pulse - 0.5) * 2 * GLOW_PULSE_AMPLITUDE

    const outerRadius = 54 * pulseScale
    const midRadius = 28 * (0.94 + pulse * 0.1)
    const coreRadius = 10 * (0.9 + pulse * 0.16)

    const outerAlpha = 0.05 + pulse * 0.025
    const midAlpha = 0.085 + pulse * 0.03
    const coreAlpha = 0.12 + pulse * 0.04

    this.pointerGlow.fillStyle(0x99f6ff, outerAlpha)
    this.pointerGlow.fillCircle(x, y, outerRadius)

    this.pointerGlow.fillStyle(0xc3ffdf, midAlpha)
    this.pointerGlow.fillCircle(x, y, midRadius)

    this.pointerGlow.fillStyle(0xffffff, coreAlpha)
    this.pointerGlow.fillCircle(x, y, coreRadius)
  }

  private updateCursorAnimation(velocityX: number, velocityY: number): void {
    const speed = Math.hypot(velocityX, velocityY)
    const isMoving = speed > 8

    if (Math.abs(velocityX) > Math.abs(velocityY)) {
      this.cursorFacing = velocityX < 0 ? 'left' : 'right'
    } else if (Math.abs(velocityY) > 1) {
      this.cursorFacing = velocityY < 0 ? 'up' : 'down'
    }

    if (this.cursorFacing === 'left') {
      this.cursor.setFlipX(true)
    } else if (this.cursorFacing === 'right') {
      this.cursor.setFlipX(false)
    }

    if (this.cursorFacing === 'up') {
      this.cursor.anims.play(
        isMoving ? 'character-walk-up' : 'character-idle-up',
        true
      )
      return
    }

    if (this.cursorFacing === 'down') {
      this.cursor.anims.play(
        isMoving ? 'character-walk-down' : 'character-idle-down',
        true
      )
      return
    }

    this.cursor.anims.play(
      isMoving ? 'character-walk-side' : 'character-idle-side',
      true
    )
  }

  private async seedInitialPath(): Promise<void> {
    try {
      await this.appendChunk()
      await this.appendChunk()
      this.statusText.setText('Hover over any node to light it')
      this.targetWorldX = this.pathNodes[0].x
      this.targetWorldY = this.pathNodes[0].y
      this.cursor.setPosition(this.pathNodes[0].x, this.pathNodes[0].y)
    } catch (error) {
      this.statusText.setText('Using calm fallback trail')
      console.warn('Failed to initialize maze-driven trail:', error)
    }
  }

  private setupAudio(): void {
    this.ambienceSound = this.sound.add('ambience', {
      loop: true,
      volume: 0.22,
    })
    this.stepSound = this.sound.add('path-step', { volume: 0.22 })
    this.chunkSound = this.sound.add('chunk-ready', { volume: 0.18 })
    this.generateSound = this.sound.add('generate', { volume: 0.06 })

    if (this.ambienceSound) {
      this.ambienceSound.play()
    }
  }

  private setupAmbientPhysics(): void {
    for (let i = 0; i < 9; i++) {
      const light = this.physics.add.image(
        Phaser.Math.Between(0, 2200),
        Phaser.Math.Between(40, this.scale.height - 40),
        'bomb'
      )

      light.setScale(0.1 + Math.random() * 0.18)
      light.setTint(i % 2 === 0 ? 0x4bc4ff : 0xffb06b)
      light.setAlpha(0.12 + Math.random() * 0.2)
      const body = light.body as Phaser.Physics.Arcade.Body | null
      if (body) {
        body.allowGravity = false
      }
      light.setBounce(1, 1)
      light.setCollideWorldBounds(true)
      light.setVelocity(
        Phaser.Math.Between(-35, 35),
        Phaser.Math.Between(-26, 26)
      )

      this.droneLights.push(light)
    }

    this.physics.world.setBounds(0, 0, 120000, this.scale.height)
  }

  private setupInput(): void {
    this.input.on('pointerdown', () => {
      if (this.ambienceSound && !this.ambienceSound.isPlaying) {
        this.ambienceSound.play()
      }
    })
  }

  private async appendChunk(): Promise<void> {
    const shouldPlayGenerateCue =
      this.pathNodes.length > 0 &&
      this.getLitCount() / this.pathNodes.length >= GENERATE_SOUND_THRESHOLD

    const chunkStartX =
      this.pathNodes.length === 0
        ? START_X
        : this.pathNodes[this.pathNodes.length - 1].x + CHUNK_GAP

    const yOffsets = await this.getMazeOffsets(
      this.chunkCounter + 11,
      NODES_PER_CHUNK
    )

    const nodes: PathNode[] = []
    for (let i = 0; i < NODES_PER_CHUNK; i++) {
      const progress = i / (NODES_PER_CHUNK - 1)
      const jitterX = Phaser.Math.Between(-NODE_JITTER_X, NODE_JITTER_X)
      const jitterY = yOffsets[i] * NODE_JITTER_Y

      const node: PathNode = {
        x: chunkStartX + progress * CHUNK_WIDTH + jitterX,
        y: Phaser.Math.Clamp(
          Phaser.Math.Between(100, this.scale.height - 100) + jitterY,
          84,
          this.scale.height - 84
        ),
        value: this.maxNodeValue + 1,
        revealed: false,
      }

      this.maxNodeValue = node.value
      nodes.push(node)
    }

    nodes.sort((a, b) => a.x - b.x)

    this.pathNodes.push(...nodes)
    this.chunkCounter++
    this.redrawWorld()

    this.chunkSound?.play()
    if (shouldPlayGenerateCue) {
      this.generateSound?.play()
    }
    this.statusText.setText(
      `Lit ${this.getLitCount()} / ${this.pathNodes.length} nodes`
    )
  }

  private async getMazeOffsets(seed: number, width: number): Promise<number[]> {
    const fallback: number[] = []
    for (let i = 0; i < width; i++) {
      fallback.push(
        (Math.sin((seed + i) * 1.7) + Math.cos(seed * 0.8 + i)) * 0.4
      )
    }

    try {
      const output = await runMazeBuilder(
        `-r3 -c6 -s${seed} -adfs -o stdout -d`
      )
      let hash = 2166136261
      for (let i = 0; i < output.length; i++) {
        hash ^= output.charCodeAt(i)
        hash = (hash * 16777619) >>> 0
      }

      const values: number[] = []
      for (let i = 0; i < width; i++) {
        hash ^= hash << 13
        hash ^= hash >>> 17
        hash ^= hash << 5
        const normalized = ((hash >>> 0) % 1000) / 1000
        values.push((normalized - 0.5) * 2)
      }

      return values
    } catch (error) {
      console.warn('Maze builder command failed, using fallback drift:', error)
      return fallback
    }
  }

  private updateHoverLighting(
    pointerWorldX: number,
    pointerWorldY: number
  ): void {
    if (this.pathNodes.length === 0) {
      return
    }

    let nextHovered = -1
    let bestDistance = HOVER_RADIUS

    for (let i = 0; i < this.pathNodes.length; i++) {
      const node = this.pathNodes[i]
      const distance = Phaser.Math.Distance.Between(
        pointerWorldX,
        pointerWorldY,
        node.x,
        node.y
      )
      if (distance < bestDistance) {
        bestDistance = distance
        nextHovered = i
      }
    }

    let needsRedraw = nextHovered !== this.hoveredNodeIndex
    this.hoveredNodeIndex = nextHovered

    if (this.hoveredNodeIndex >= 0) {
      const hoveredNode = this.pathNodes[this.hoveredNodeIndex]
      if (!hoveredNode.revealed) {
        hoveredNode.revealed = true
        this.furthestLitX = Math.max(this.furthestLitX, hoveredNode.x)
        this.stepSound?.play()
        this.addSpriteBurst(hoveredNode.x, hoveredNode.y)
        needsRedraw = true
        this.statusText.setText(
          `Lit ${this.getLitCount()} / ${this.pathNodes.length} nodes`
        )
      }
    }

    if (needsRedraw) {
      this.redrawWorld()
    }
  }

  private emitPointerTrail(x: number, y: number): void {
    if (this.time.now < this.nextTrailEmitAt) {
      return
    }

    if (
      Phaser.Math.Distance.Between(this.lastTrailX, this.lastTrailY, x, y) < 14
    ) {
      return
    }

    this.lastTrailX = x
    this.lastTrailY = y
    this.nextTrailEmitAt = this.time.now + 24

    const particle = this.add.sprite(
      x + Phaser.Math.Between(-8, 8),
      y + Phaser.Math.Between(-8, 8),
      'explosion',
      Phaser.Math.Between(0, 15)
    )

    particle.setDepth(19)
    particle.setBlendMode(Phaser.BlendModes.ADD)
    particle.setScale(Phaser.Math.FloatBetween(0.08, 0.18))
    particle.setAlpha(0.45)

    this.tweens.add({
      targets: particle,
      alpha: 0,
      scale: particle.scale * 1.5,
      duration: 260,
      ease: 'Sine.Out',
      onComplete: () => particle.destroy(),
    })
  }

  private addSpriteBurst(x: number, y: number): void {
    const burst = this.add.sprite(x, y, 'explosion', 0)
    burst.setScale(0.18)
    burst.setAlpha(0.6)

    this.tweens.add({
      targets: burst,
      alpha: 0,
      duration: 400,
      onComplete: () => burst.destroy(),
    })
  }

  private redrawWorld(): void {
    this.gridGraphics.clear()
    this.gridGraphics.lineStyle(1, 0x6aa9bf, 0.16)

    for (const node of this.pathNodes) {
      this.gridGraphics.strokeRect(node.x - 34, node.y - 34, 68, 68)
    }

    this.hiddenPathGraphics.clear()
    this.hiddenPathGraphics.lineStyle(6, 0xb6d4db, 0.14)

    this.revealedPathGraphics.clear()
    this.revealedPathGraphics.lineStyle(8, 0x9cffd2, 0.88)

    for (let i = 1; i < this.pathNodes.length; i++) {
      const prev = this.pathNodes[i - 1]
      const current = this.pathNodes[i]

      this.hiddenPathGraphics.beginPath()
      this.hiddenPathGraphics.moveTo(prev.x, prev.y)
      this.hiddenPathGraphics.lineTo(current.x, current.y)
      this.hiddenPathGraphics.strokePath()

      if (prev.revealed && current.revealed) {
        this.revealedPathGraphics.beginPath()
        this.revealedPathGraphics.moveTo(prev.x, prev.y)
        this.revealedPathGraphics.lineTo(current.x, current.y)
        this.revealedPathGraphics.strokePath()
      }
    }

    this.nodeGraphics.clear()

    for (let i = 0; i < this.pathNodes.length; i++) {
      const node = this.pathNodes[i]
      const isHovered = i === this.hoveredNodeIndex
      const hue = (node.value % 12) / 12
      const baseColor = Phaser.Display.Color.HSVToRGB(
        hue,
        0.45,
        node.revealed ? 0.95 : 0.55
      ).color
      const fill = isHovered ? 0xfef38a : baseColor
      const alpha = isHovered ? 0.98 : node.revealed ? 0.82 : 0.42
      const radius = isHovered ? 21 : node.revealed ? 16 : 13

      this.nodeGraphics.fillStyle(fill, alpha)
      this.nodeGraphics.fillCircle(node.x, node.y, radius)
    }
  }

  private getLitCount(): number {
    let litCount = 0
    for (const node of this.pathNodes) {
      if (node.revealed) {
        litCount++
      }
    }

    return litCount
  }

  shutdown(): void {
    this.scale.off('resize', this.handleResize, this)
    this.ambienceSound?.stop()
    this.ambienceSound?.destroy()
    this.stepSound?.destroy()
    this.chunkSound?.destroy()
    this.generateSound?.destroy()

    for (const light of this.droneLights) {
      light.destroy()
    }
    this.droneLights = []
  }
}
