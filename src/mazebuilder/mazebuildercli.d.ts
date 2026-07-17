// TypeScript bindings for emscripten-generated code.  Automatically generated at compile time.
interface WasmModule {
  _main(_0: number, _1: number): number
}

type EmbindString =
  ArrayBuffer | Uint8Array | Uint8ClampedArray | Int8Array | string
export interface ClassHandle {
  isAliasOf(other: ClassHandle): boolean
  delete(): void
  deleteLater(): this
  isDeleted(): boolean
  // @ts-ignore - If targeting lower than ESNext, this symbol might not exist.
  [Symbol.dispose](): void
  clone(): this
}
export interface cli extends ClassHandle {
  help(): string
  version(): string
  run(_0: EmbindString): string
}

export interface StringVector extends ClassHandle, Iterable<string> {
  push_back(_0: EmbindString): void
  resize(_0: number, _1: EmbindString): void
  size(): number
  get(_0: number): string | undefined
  set(_0: number, _1: EmbindString): boolean
}

interface EmbindModule {
  cli: {}
  get(): cli | null
  StringVector: {
    new (): StringVector
  }
}

export type MainModule = WasmModule & EmbindModule
export default function MainModuleFactory(
  options?: unknown
): Promise<MainModule>
