import type { MainModule } from './mazebuildercli'

let activeModule: MainModule | null = null
let pendingModule: Promise<MainModule> | null = null
let cachedFactory: ((options?: unknown) => Promise<MainModule>) | null = null
let hasPatchedTextDecoder = false

const isResizableArrayBuffer = (buffer: ArrayBufferLike): boolean => {
  const candidate = buffer as { resizable?: boolean }
  return candidate.resizable === true
}

const installTextDecoderGuard = (): void => {
  if (hasPatchedTextDecoder) {
    return
  }

  const originalDecode = TextDecoder.prototype.decode

  TextDecoder.prototype.decode = function (
    input?: BufferSource,
    options?: TextDecodeOptions
  ): string {
    if (
      input &&
      ArrayBuffer.isView(input) &&
      isResizableArrayBuffer(input.buffer)
    ) {
      const view = input as Uint8Array
      const stableCopy = new Uint8Array(view.byteLength)
      stableCopy.set(
        new Uint8Array(view.buffer, view.byteOffset, view.byteLength)
      )
      return originalDecode.call(this, stableCopy, options)
    }

    if (input instanceof ArrayBuffer && isResizableArrayBuffer(input)) {
      return originalDecode.call(this, input.slice(0), options)
    }

    return originalDecode.call(this, input, options)
  }

  hasPatchedTextDecoder = true
}

const getFactory = async (): Promise<
  (options?: unknown) => Promise<MainModule>
> => {
  if (cachedFactory) {
    return cachedFactory
  }

  const modulePath = new URL(
    `${import.meta.env.BASE_URL}mazebuilder/mazebuildercli.js`,
    window.location.href
  ).toString()

  const imported = await import(/* @vite-ignore */ modulePath)

  const factory = imported.default as
    ((options?: unknown) => Promise<MainModule>) | undefined

  if (!factory) {
    throw new Error(
      'Emscripten Module factory not found in mazebuildercli.js default export'
    )
  }

  cachedFactory = factory
  return cachedFactory
}

export const getMazeModule = async (): Promise<MainModule> => {
  if (activeModule) {
    return activeModule
  }

  if (pendingModule) {
    return pendingModule
  }

  installTextDecoderGuard()
  pendingModule = (async () => {
    const factory = await getFactory()
    const module = await factory({ noInitialRun: true })

    if (!module || !module.get) {
      throw new Error('Failed to initialize maze builder module')
    }

    activeModule = module
    return module
  })()

  try {
    return await pendingModule
  } finally {
    pendingModule = null
  }
}

export const runMazeBuilder = async (command: string): Promise<string> => {
  const module = await getMazeModule()
  const cli = module.get()

  if (!cli) {
    throw new Error('Maze builder CLI instance is null')
  }

  try {
    return cli.run(command)
  } finally {
    cli.delete()
  }
}
