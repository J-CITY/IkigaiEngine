"""Render the four LinkedIn Pulse charts from embedded benchmark medians.

Requires Python 3.10+ and matplotlib. Run with --help for styling options.
Data below comes from testModule/benchmarks/ecs/RESULTS.md. Values retain
the report precision, while bar labels use the original article precision.
Each panel has its own linear axis starting at zero.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import sys
import textwrap


@dataclass(frozen=True)
class Bar:
    label: str
    value: float
    color: str


@dataclass(frozen=True)
class Panel:
    title: str
    bars: tuple[Bar, ...]
    decimals: int = 2


@dataclass(frozen=True)
class Chart:
    title: str
    unit: str
    panels: tuple[Panel, ...]
    note: str


# Edit data, labels and captions here. All values are nanoseconds.
# Historical data from 16 November 2026, with ComponentArray chunks of 100.
CHARTS = {
    "iteration": Chart(
        "Обновление позиции по скорости",
        "нс на сущность",
        (
            Panel("Изменение Ikigai", (
                Bar("Ikigai исходный", 3.751, "baseline"),
                Bar("Ikigai итоговый", 0.463, "ikigai"),
            )),
            Panel("Итоговый прогон", (
                Bar("Ikigai", 0.463, "ikigai"),
                Bar("EnTT view", 1.530, "entt"),
                Bar("Flecs без кэша", 0.401, "flecs"),
            )),
        ),
        "100 000 сущностей · 100% совпадений · 1 архетип · 5 повторов, медианы. "
        "Слева исходная и итоговая серии, справа итоговая серия. "
        "Источники: baseline.json, ecs-target-growth-comparison.json.",
    ),
    "mixed": Chart(
        "Архетипная позиция + sparse-скорость",
        "нс на совпавшую сущность",
        (
            Panel("10% совпадений", (
                Bar("Ikigai исходный", 141.437, "baseline"),
                Bar("Ikigai итоговый", 2.554, "ikigai"),
            )),
            Panel("50% совпадений", (
                Bar("Ikigai исходный", 21.277, "baseline"),
                Bar("Ikigai итоговый", 1.639, "ikigai"),
            )),
        ),
        "100 000 сущностей · 5 повторов, медианы исходной и итоговой серий Ikigai. "
        "Источники: baseline.json, ecs-target-growth-comparison.json.",
    ),
    "lifecycle": Chart(
        "Жизненный цикл пустых сущностей",
        "нс на сущность",
        (
            Panel("Создание пустой сущности", (
                Bar("Ikigai исходный", 277.841, "baseline"),
                Bar("Ikigai итоговый", 7.278, "ikigai"),
                Bar("EnTT итоговый", 17.347, "entt"),
                Bar("Flecs итоговый", 21.449, "flecs"),
            ), decimals=1),
            Panel("Удаление пустой сущности", (
                Bar("Ikigai исходный", 143.247, "baseline"),
                Bar("Ikigai итоговый", 7.296, "ikigai"),
                Bar("EnTT итоговый", 21.412, "entt"),
                Bar("Flecs итоговый", 22.792, "flecs"),
            ), decimals=1),
        ),
        "100 000 операций · 5 повторов, медианы. Исходный Ikigai из baseline.json, "
        "остальные значения из ecs-target-growth-comparison.json.",
    ),
    "batches": Chart(
        "Добавление компонента с переносом 8 колонок",
        "нс на выбранную сущность",
        (
            Panel("Вся таблица, 100 000 строк", (
                Bar("Ikigai пакет", 14.184, "ikigai"),
                Bar("EnTT диапазон", 15.339, "entt"),
                Bar("Flecs поштучно", 122.383, "flecs"),
            ), decimals=1),
            Panel("Перемешанные 10%, 10 000 строк", (
                Bar("Ikigai пакет", 138.822, "ikigai"),
                Bar("EnTT диапазон", 19.971, "entt"),
                Bar("Flecs поштучно", 341.365, "flecs"),
            ), decimals=1),
        ),
        "Источник — 100 000 сущностей. Слева 5 повторов, справа 7 с привязкой к CPU 18. "
        "Медианы, резервирование включено. Источники: archetype-batch-chunked.json, "
        "scatter-focus-pinned.json.",
    ),
}

# Palette order for --colors: baseline, ikigai, entt, flecs.
THEMES = {
    "light": {
        "background": "#ffffff", "foreground": "#17212b", "muted": "#526172",
        "grid": "#e4e9ee", "colors": ("#a2adbc", "#176d86", "#e29532", "#7664ae"),
    },
    "dark": {
        "background": "#111923", "foreground": "#eff5fb", "muted": "#b4c2d2",
        "grid": "#334250", "colors": ("#8392a5", "#57c8dd", "#ffbd69", "#b9a3f5"),
    },
    "paper": {
        "background": "#fbf8f1", "foreground": "#302e2a", "muted": "#656057",
        "grid": "#e5dfd3", "colors": ("#b0aaa0", "#287f74", "#be733a", "#807093"),
    },
}


def positive_float(value: str) -> float:
    number = float(value)
    if not 0 < number < float("inf"):
        raise argparse.ArgumentTypeError("expected a finite positive number")
    return number


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--charts", nargs="+", choices=CHARTS, default=list(CHARTS))
    parser.add_argument("--theme", choices=THEMES, default="light")
    parser.add_argument("--output-dir", type=Path, help="default: <repo>/build/ecs-charts/<theme>")
    parser.add_argument("--format", nargs="+", choices=("png", "svg", "pdf"), default=["png"])
    parser.add_argument("--dpi", type=positive_float, default=160)
    parser.add_argument("--figsize", nargs=2, type=positive_float, default=(16, 7), metavar=("WIDTH", "HEIGHT"))
    parser.add_argument("--font", default="DejaVu Sans", help="installed font with Cyrillic support")
    parser.add_argument("--font-size", type=positive_float, default=12)
    parser.add_argument("--colors", nargs=4, metavar=("BASELINE", "IKIGAI", "ENTT", "FLECS"))
    parser.add_argument("--mpl-style", help="Matplotlib style name or path to a .mplstyle file")
    parser.add_argument("--list", action="store_true", help="list chart names and exit")
    return parser.parse_args()


def render_chart(plt, name: str, chart: Chart, args: argparse.Namespace, theme: dict, palette: dict, output_dir: Path) -> None:
    fig, axes = plt.subplots(1, len(chart.panels), figsize=args.figsize, squeeze=False)
    # Extra room between panels accommodates their independent category labels.
    fig.subplots_adjust(left=0.15, right=0.97, top=0.74, bottom=0.25, wspace=0.72)
    fig.suptitle(chart.title, x=0.045, y=0.96, ha="left", fontsize=args.font_size * 1.8, weight="bold")
    for ax, panel in zip(axes[0], chart.panels):
        values = [bar.value for bar in panel.bars]
        bars = ax.barh(range(len(panel.bars)), values, color=[palette[bar.color] for bar in panel.bars], height=0.58)
        ax.set_yticks(range(len(panel.bars)), [bar.label for bar in panel.bars])
        ax.invert_yaxis()
        ax.set_xlim(0, max(values) * 1.28)
        ax.set_title(textwrap.fill(panel.title, width=31), loc="left", pad=20, weight="bold", fontsize=args.font_size * 1.2)
        ax.set_xlabel(chart.unit, labelpad=10)
        ax.set_axisbelow(True)
        ax.grid(axis="x", color=theme["grid"], linewidth=0.8)
        ax.tick_params(axis="y", length=0, pad=10)
        ax.spines[["top", "right", "left"]].set_visible(False)
        labels = [f"{value:.{panel.decimals}f}".replace(".", ",") for value in values]
        ax.bar_label(bars, labels=labels, padding=7, weight="bold")
    footer = chart.note + " Линейные шкалы панелей различаются. Меньше — быстрее."
    fig.text(0.045, 0.055, textwrap.fill(footer, width=135), color=theme["muted"], fontsize=args.font_size * 0.75, va="bottom")
    for extension in args.format:
        destination = output_dir / f"{name}.{extension}"
        fig.savefig(destination, dpi=args.dpi, facecolor=theme["background"], bbox_inches="tight", pad_inches=0.22)
        print(destination)
    plt.close(fig)


def main() -> None:
    sys.stdout.reconfigure(encoding="utf-8")
    args = parse_args()
    if args.list:
        for name, chart in CHARTS.items():
            print(f"{name}: {chart.title}")
        return
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        from matplotlib.colors import is_color_like
    except ImportError as error:
        raise SystemExit("Install matplotlib: python -m pip install matplotlib") from error

    theme = THEMES[args.theme]
    colors = args.colors or theme["colors"]
    if not all(is_color_like(color) for color in colors):
        raise SystemExit("--colors requires four valid Matplotlib colors, such as '#176d86'")
    palette = dict(zip(("baseline", "ikigai", "entt", "flecs"), colors))
    output_dir = args.output_dir or Path(__file__).resolve().parents[2] / "build" / "ecs-charts" / args.theme
    output_dir.mkdir(parents=True, exist_ok=True)
    plt.style.use(args.mpl_style or "default")
    plt.rcParams.update({
        "font.family": args.font, "font.size": args.font_size,
        "figure.facecolor": theme["background"], "axes.facecolor": theme["background"],
        "text.color": theme["foreground"], "axes.labelcolor": theme["foreground"],
        "axes.edgecolor": theme["foreground"], "xtick.color": theme["foreground"],
        "ytick.color": theme["foreground"], "svg.fonttype": "none", "pdf.fonttype": 42,
    })
    for name in args.charts:
        render_chart(plt, name, CHARTS[name], args, theme, palette, output_dir)


if __name__ == "__main__":
    main()
