from dataclasses import dataclass


@dataclass
class Fluid:
    name: str
    id: str
    material: str = ""
    sources: str = ""


@dataclass
class Solid:
    name: str
    id: str
    material: str = ""
    sources: str = ""


@dataclass
class VelocityInlet:
    name: str
    id: str
    vmag: str = ""
    t: str = ""
    turb_intensity: str = ""
    turb_hydraulic_diam: str = ""
    turb_viscosity_ratio: str = ""


@dataclass
class MassFlowInlet:
    name: str
    id: str
    mass_flow: str = ""
    t: str = ""
    turb_intensity: str = ""
    turb_hydraulic_diam: str = ""
    turb_viscosity_ratio: str = ""


@dataclass
class MassFlowOutlet:
    name: str
    id: str
    mass_flow: str = ""
    t: str = ""
    turb_intensity: str = ""
    turb_hydraulic_diam: str = ""
    turb_viscosity_ratio: str = ""


@dataclass
class PressureOutlet:
    name: str
    id: str
    p: str = ""
    t: str = ""
    turb_intensity: str = ""
    turb_hydraulic_diam: str = ""
    turb_viscosity_ratio: str = ""


@dataclass
class Wall:
    name: str
    id: str
    material: str = ""
    t: str = ""
    q: str = ""
    h: str = ""


@dataclass
class Interior:
    name: str
    id: str


@dataclass
class PorousJump:
    name: str
    id: str
    alpha: str = ""
    dm: str = ""
    c2: str = ""


class BoundaryFactory:
    @staticmethod
    def create(name: str, id: str, type_: str):
        match type_:
            case "fluid":
                return Fluid(name, id)
            case "solid":
                return Solid(name, id)
            case "velocity-inlet":
                return VelocityInlet(name, id)
            case "mass-flow-inlet":
                return MassFlowInlet(name, id)
            case "mass-flow-outlet":
                return MassFlowOutlet(name, id)
            case "pressure-outlet":
                return PressureOutlet(name, id)
            case "wall":
                return Wall(name, id)
            case "interior":
                return Interior(name, id)
            case "porous-jump":
                return PorousJump(name, id)
            case _:
                return None
