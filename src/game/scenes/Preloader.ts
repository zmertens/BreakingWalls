import { Scene } from 'phaser'

const CHARACTER_FRAME_SIZE = 128
const CHARACTER_ROWS = 8
const FRAMES_PER_COLOR = 9
const PLAYER_COLOR_INDEX = 2

function frameFromColumnMajorPosition(position: number): number {
  const col = Math.floor(position / CHARACTER_ROWS)
  const row = position % CHARACTER_ROWS
  return row * CHARACTER_ROWS + col
}

function framesForColor(colorIndex: number): number[] {
  const start = colorIndex * FRAMES_PER_COLOR
  return Array.from({ length: FRAMES_PER_COLOR }, (_, i) =>
    frameFromColumnMajorPosition(start + i)
  )
}

export class Preloader extends Scene {
  private backgroundImage!: Phaser.GameObjects.Image
  private progressOutline!: Phaser.GameObjects.Rectangle
  private progressBar!: Phaser.GameObjects.Rectangle
  private progressValue = 0

  constructor() {
    super('Preloader')
  }

  init() {
    this.backgroundImage = this.add.image(0, 0, 'background').setOrigin(0.5)
    this.progressOutline = this.add
      .rectangle(0, 0, 468, 32)
      .setOrigin(0.5)
      .setStrokeStyle(1, 0xffffff)
    this.progressBar = this.add
      .rectangle(0, 0, 4, 28, 0xffffff)
      .setOrigin(0, 0.5)

    this.layoutPreloader(this.scale.width, this.scale.height)
    this.scale.on('resize', this.handleResize, this)
    this.events.once('shutdown', () => {
      this.scale.off('resize', this.handleResize, this)
    })

    //  Use the 'progress' event emitted by the LoaderPlugin to update the loading bar
    this.load.on('progress', (progress: number) => {
      this.progressValue = progress
      this.progressBar.width = 4 + 460 * progress
    })
  }

  private handleResize(gameSize: Phaser.Structs.Size): void {
    this.layoutPreloader(gameSize.width, gameSize.height)
  }

  private layoutPreloader(width: number, height: number): void {
    this.backgroundImage.setPosition(width * 0.5, height * 0.5)
    this.backgroundImage.setDisplaySize(width, height)

    this.progressOutline.setPosition(width * 0.5, height * 0.5)
    this.progressBar.setPosition(width * 0.5 - 230, height * 0.5)
    this.progressBar.width = 4 + 460 * this.progressValue
  }

  preload() {
    //  Load the assets for the game - Replace with your own assets
    this.load.setPath('assets')

    this.load.image('logo', 'FlipsAndAleIcon.png')
    this.load.image('maze-bg', 'maze.png')
    this.load.image('bomb', 'bomb.png')
    this.load.spritesheet('explosion', 'Explosion.png', {
      frameWidth: 192,
      frameHeight: 192,
    })
    this.load.spritesheet('characters', 'spritesheet-characters-default.png', {
      frameWidth: CHARACTER_FRAME_SIZE,
      frameHeight: CHARACTER_FRAME_SIZE,
    })

    this.load.audio('ambience', ['game-menu_remixed.mp3'])
    this.load.audio('generate', ['generate.ogg'])
    this.load.audio('path-step', ['sfx_select.ogg'])
    this.load.audio('chunk-ready', ['sfx_throw.ogg'])
  }

  create() {
    const colorFrames = framesForColor(PLAYER_COLOR_INDEX)

    // 9-frame layout per color: [down0, down1, down2, side0, side1, side2, up0, up1, up2]
    this.anims.create({
      key: 'character-idle-down',
      frames: [{ key: 'characters', frame: colorFrames[1] }],
      frameRate: 1,
      repeat: -1,
    })
    this.anims.create({
      key: 'character-walk-down',
      frames: [colorFrames[0], colorFrames[1], colorFrames[2]].map((frame) => ({
        key: 'characters',
        frame,
      })),
      frameRate: 10,
      repeat: -1,
    })

    this.anims.create({
      key: 'character-idle-up',
      frames: [{ key: 'characters', frame: colorFrames[7] }],
      frameRate: 1,
      repeat: -1,
    })
    this.anims.create({
      key: 'character-walk-up',
      frames: [colorFrames[6], colorFrames[7], colorFrames[8]].map((frame) => ({
        key: 'characters',
        frame,
      })),
      frameRate: 10,
      repeat: -1,
    })

    this.anims.create({
      key: 'character-idle-side',
      frames: [{ key: 'characters', frame: colorFrames[4] }],
      frameRate: 1,
      repeat: -1,
    })
    this.anims.create({
      key: 'character-walk-side',
      frames: [colorFrames[3], colorFrames[4], colorFrames[5]].map((frame) => ({
        key: 'characters',
        frame,
      })),
      frameRate: 10,
      repeat: -1,
    })

    this.scale.off('resize', this.handleResize, this)
    this.scene.start('MainMenu')
  }
}
