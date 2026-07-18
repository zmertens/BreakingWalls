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
  constructor() {
    super('Preloader')
  }

  init() {
    //  We loaded this image in our Boot Scene, so we can display it here
    this.add.image(512, 384, 'background')

    //  A simple progress bar. This is the outline of the bar.
    this.add.rectangle(512, 384, 468, 32).setStrokeStyle(1, 0xffffff)

    //  This is the progress bar itself. It will increase in size from the left based on the % of progress.
    const bar = this.add.rectangle(512 - 230, 384, 4, 28, 0xffffff)

    //  Use the 'progress' event emitted by the LoaderPlugin to update the loading bar
    this.load.on('progress', (progress: number) => {
      //  Update the progress bar (our bar is 464px wide, so 100% = 464px)
      bar.width = 4 + 460 * progress
    })
  }

  preload() {
    //  Load the assets for the game - Replace with your own assets
    this.load.setPath('assets')

    this.load.image('logo', 'logo.png')
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

    this.scene.start('MainMenu')
  }
}
