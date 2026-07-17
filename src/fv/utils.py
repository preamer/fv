# From context.scm
FLUENT_ENUM = {
    "0": "First Order Upwind",
    "1": "Second Order Upwind",
    "2": "Power Law",
    "3": "Central Difference",
    "4": "Quick",
    "5": "Modified HRIC",
    "6": "Third-Order MUSCL",
    "7": "Bounded Central Differencing",
    "8": "CICSAM",
    "9": "Low Diffusion Second Order",
    "10": "Standard",
    "11": "Linear",
    "12": "Second Order",
    "13": "Body Force Weighted",
    "14": "PRESTO!",
    "15": "Continuity Based",
    "16": "Geo-Reconstruct",
    "17": "Donor-Acceptor",
    "18": "Modified Body Force Weighted",
    "20": "SIMPLE",
    "21": "SIMPLEC",
    "22": "PISO",
    "23": "Phase Coupled SIMPLE",
    "24": "Coupled",
    "25": "Fractional Step",
    "28": "Compressive",
    "29": "BGM",
    "30": "Phase Coupled PISO",
    "31": "Low Diffusion Central",
}

FLUENT_BOUNDARY_TYPES = {
    2: "interior",
    3: "wall",
    4: "inlet-vent",
    4: "pressure-inlet",
    4: "intake-fan",
    5: "pressure-outlet",
    5: "outlet-vent",
    5: "exhaust-fan",
    7: "symmetry",
    9: "pressure-far-field",
    10: "velocity-inlet",
    14: "radiator",
    14: "fan",
    14: "porous-jump",
    20: "mass-flow-inlet",
    20: "mass-flow-outlet",
    24: "interface",
    25: "overset",
    36: "outflow",
    37: "axis",
}

CELL_TYPES = {
    1: "Triangle",
    2: "Tetrahedron",
    3: "Quadrilateral",
    4: "Hexahedral",
    5: "Pyramid",
    6: "Wedge",
    7: "Polyhedron",
}


def print_colored_dict(data) -> None:
    """Print nested data in 4-space JSON format.

    Keys (including colon) : cycle through 6 hued colours per nesting level.
    String values          : bright white  — no hue, never clashes with any key colour.
    Numbers / bool / null  : white-grey    — no hue, never clashes with any key colour.
    """
    # 6 hued colours used exclusively for keys
    LEVEL_COLORS = [
        "\033[36m",  # cyan
        "\033[32m",  # green
        "\033[33m",  # yellow
        "\033[35m",  # magenta
        "\033[34m",  # blue
        "\033[31m",  # red
    ]
    STR_COLOR = "\033[97m"  # bright white — string values
    PRIM_COLOR = "\033[37m"  # white-grey   — numbers / booleans / null
    RESET = "\033[0m"

    import re
    import json

    # Three capture groups: key (with quotes) | colon + whitespace | value (with optional trailing comma)
    KV_RE = re.compile(r'^("(?:[^"\\]|\\.)*")(:\s*)(.+)$')

    text = json.dumps(data, indent=4, ensure_ascii=False, default=str)

    for line in text.splitlines():
        content = line.lstrip(" ")
        level = (len(line) - len(content)) // 4
        indent = " " * (len(line) - len(content))
        lc = LEVEL_COLORS[level % len(LEVEL_COLORS)]

        m = KV_RE.match(content)
        if m:
            key, colon, rest = m.group(1), m.group(2), m.group(3)
            # Strip trailing comma only for type detection; print the original `rest`
            bare = rest.rstrip(",").strip()

            if bare in ("{", "["):
                # Value is an opening bracket — keep structural chars in the level colour
                print(f"{indent}{lc}{key}{colon}{rest}{RESET}")
            elif bare.startswith('"'):
                print(f"{indent}{lc}{key}{colon}{RESET}{STR_COLOR}{rest}{RESET}")
            else:
                # Number / boolean / null
                print(f"{indent}{lc}{key}{colon}{RESET}{PRIM_COLOR}{rest}{RESET}")
        else:
            # Structural line ({ } [ ]) or bare array element
            bare = content.rstrip(",").strip()
            if bare.startswith('"'):
                print(f"{indent}{STR_COLOR}{content}{RESET}")
            elif bare in ("{", "}", "[", "]"):
                print(f"{indent}{lc}{content}{RESET}")
            else:
                # Number / boolean / null inside an array
                print(f"{indent}{PRIM_COLOR}{content}{RESET}")
