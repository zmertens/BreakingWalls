import * as Phaser from 'phaser'
import { Scene, GameObjects } from 'phaser'

export class MainMenu extends Scene {
  background!: GameObjects.Image
  logo!: GameObjects.Image
  title!: GameObjects.Text

  constructor() {
    super('MainMenu')
  }

  create() {
    this.background = this.add.image(0, 0, 'background').setOrigin(0.5)
    this.background.setAlpha(0.85)

    this.logo = this.add.image(0, 0, 'logo').setOrigin(0.5)

    this.title = this.add
      .text(0, 0, 'Breaking Walls\nTouch / Click To Begin', {
        fontFamily: 'Verdana',
        fontSize: 34,
        color: '#e8f6ff',
        stroke: '#000000',
        strokeThickness: 8,
        align: 'center',
      })
      .setOrigin(0.5)

    this.layoutMenu(this.scale.width, this.scale.height)
    this.scale.on('resize', this.handleResize, this)
    this.events.once('shutdown', () => {
      this.scale.off('resize', this.handleResize, this)
    })

    this.input.once('pointerdown', () => {
      this.scene.start('Game')
    })
  }

  private handleResize(gameSize: Phaser.Structs.Size): void {
    this.layoutMenu(gameSize.width, gameSize.height)
  }

  private layoutMenu(width: number, height: number): void {
    const uiScale = Phaser.Math.Clamp(height / 768, 0.7, 1.35)

    this.background.setPosition(width * 0.5, height * 0.5)
    this.background.setDisplaySize(width, height)

    this.logo.setPosition(width * 0.5, height * 0.4)
    this.logo.setScale(uiScale)

    this.title.setPosition(width * 0.5, height * 0.68)
    this.title.setFontSize(Math.round(34 * uiScale))
    this.title.setStroke('#000000', Math.max(4, Math.round(8 * uiScale)))
  }
}
