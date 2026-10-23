def can_build(env, platform):
    # Hosts libretro emulator cores (the Xbox build of Socket runs its retro cores in-process).
    return platform in ("windows", "linuxbsd", "macos")


def configure(env):
    pass
