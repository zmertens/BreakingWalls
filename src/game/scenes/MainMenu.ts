import { Scene, GameObjects } from 'phaser'

export class MainMenu extends Scene {
  background: GameObjects.Image
  logo: GameObjects.Image
  title: GameObjects.Text

  constructor() {
    super('MainMenu')
  }

  create() {
    this.background = this.add.image(512, 384, 'background')
    this.background.setAlpha(0.85)

    this.logo = this.add.image(512, 300, 'logo')

    this.title = this.add
      .text(512, 468, 'Maze Drift\nTouch / Click To Begin', {
        fontFamily: 'Verdana',
        fontSize: 34,
        color: '#e8f6ff',
        stroke: '#000000',
        strokeThickness: 8,
        align: 'center',
      })
      .setOrigin(0.5)

    this.input.once('pointerdown', () => {
      this.scene.start('Game')
    })
  }
}
