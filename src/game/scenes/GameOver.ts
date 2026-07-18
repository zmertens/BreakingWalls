import { Scene } from 'phaser'

export class GameOver extends Scene {
  camera!: Phaser.Cameras.Scene2D.Camera
  background!: Phaser.GameObjects.Image
  gameoverText!: Phaser.GameObjects.Text

  constructor() {
    super('GameOver')
  }

  create() {
    this.camera = this.cameras.main
    this.camera.setBackgroundColor(0xff0000)

    this.background = this.add.image(0, 0, 'background').setOrigin(0.5)
    this.background.setAlpha(0.5)

    this.gameoverText = this.add.text(0, 0, 'Game Over', {
      fontFamily: 'Arial Black',
      fontSize: 64,
      color: '#ffffff',
      stroke: '#000000',
      strokeThickness: 8,
      align: 'center',
    })
    this.gameoverText.setOrigin(0.5)

    this.layoutGameOver(this.scale.width, this.scale.height)
    this.scale.on('resize', this.handleResize, this)
    this.events.once('shutdown', () => {
      this.scale.off('resize', this.handleResize, this)
    })

    this.input.once('pointerdown', () => {
      this.scene.start('MainMenu')
    })
  }

  private handleResize(gameSize: Phaser.Structs.Size): void {
    this.layoutGameOver(gameSize.width, gameSize.height)
  }

  private layoutGameOver(width: number, height: number): void {
    const uiScale = Phaser.Math.Clamp(height / 768, 0.7, 1.4)

    this.background.setPosition(width * 0.5, height * 0.5)
    this.background.setDisplaySize(width, height)

    this.gameoverText.setPosition(width * 0.5, height * 0.5)
    this.gameoverText.setFontSize(Math.round(64 * uiScale))
    this.gameoverText.setStroke('#000000', Math.max(4, Math.round(8 * uiScale)))
  }
}
