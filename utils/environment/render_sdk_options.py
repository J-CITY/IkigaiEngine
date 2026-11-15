"""Shared Windows SR/FG build options for run_vs and environment setup."""

# Support direct CLI invocation from any working directory.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

from dataclasses import dataclass


@dataclass(frozen=True)
class RenderSdkOptions:
    dlss_sr: bool = False
    fsr_sr: bool = False
    dlss_fg: bool = False
    fsr_fg: bool = False
    streamline_enabled: bool | None = None

    @property
    def enabled(self):
        return self.dlss_sr or self.fsr_sr or self.dlss_fg or self.fsr_fg or self.streamline

    @property
    def streamline(self):
        return self.dlss_fg if self.streamline_enabled is None else self.streamline_enabled

    def validate(self, platform, backends, architecture='x64'):
        if not self.enabled:
            return
        if platform != 'win':
            raise ValueError('DLSS/FSR SR and FG currently require -p win')
        if architecture != 'x64':
            raise ValueError('DLSS/FSR SDKs require -a x64')
        if not {'vulkan', 'dx12'}.intersection(backends):
            raise ValueError('DLSS/FSR require -g vulkan and/or dx12; OpenGL alone is unsupported')

    def sdk_names(self, backends):
        names = []
        if self.dlss_sr:
            names.append('dlss')
        if self.fsr_sr or self.fsr_fg:
            if 'vulkan' in backends:
                names.append('fsr-vulkan')
            if 'dx12' in backends:
                names.append('fsr-dx12')
        if self.streamline:
            names.append('streamline')
        return names

    def cmake_flags(self):
        return [f'-D{name}={"ON" if enabled else "OFF"}' for name, enabled in (
            ('USE_DLSS', self.dlss_sr),
            ('USE_FSR', self.fsr_sr),
            ('USE_DLSS_FRAME_GENERATION', self.dlss_fg),
            ('USE_FSR_FRAME_GENERATION', self.fsr_fg),
            ('USE_STREAMLINE', self.streamline),
        )]


def add_render_sdk_arguments(parser):
    group = parser.add_argument_group('Windows Vulkan/DX12 SR and frame generation (default: off)')
    for flag, label in (('dlss-sr', 'DLSS Super Resolution'), ('fsr-sr', 'FSR Super Resolution'),
                        ('dlss-fg', 'DLSS Frame Generation / MFG (enables Streamline)'),
                        ('fsr-fg', 'FSR Frame Generation')):
        group.add_argument('--' + flag, action='store_true', help='Compile ' + label)


def render_sdk_options(args):
    return RenderSdkOptions(args.dlss_sr, args.fsr_sr, args.dlss_fg, args.fsr_fg)
