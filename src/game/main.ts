import { Boot } from './scenes/Boot'
import { GameOver } from './scenes/GameOver'
import { Game as MainGame } from './scenes/Game.ts'
import { MainMenu } from './scenes/MainMenu'
import { AUTO, Game } from 'phaser'
import { Scale } from 'phaser'
import { Preloader } from './scenes/Preloader'

//  Find out more information about the Game Config at:
//  https://docs.phaser.io/api-documentation/typedef/types-core#gameconfig
const config: Phaser.Types.Core.GameConfig = {
  type: AUTO,
  parent: 'game-container',
  scale: {
    mode: Scale.RESIZE,
    width: '100%',
    height: '100%',
  },
  physics: {
    default: 'arcade',
    arcade: {
      gravity: { x: 0, y: 300 },
      debug: false,
    },
  },
  backgroundColor: '#028af8',
  scene: [Boot, Preloader, MainMenu, MainGame, GameOver],
}

const StartGame = (parent: string): Game => {
  return new Game({ ...config, parent })
}

export default StartGame
