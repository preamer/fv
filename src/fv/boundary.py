from dataclasses import dataclass


@dataclass
class Fluid:
    name: str
    id_: str
    material: str = ''
    sources: str = ''
    fixed: str = ''
    mrf_motion: str = ''
    mgrid_motion: str = ''
    solid_motion: str = ''
    laminar: str = ''
    porous: str = ''
    fanzone: str = ''


@dataclass
class Solid:
    name: str
    id_: str
    material: str = ''
    sources: str = ''


@dataclass
class VelocityInlet:
    name: str
    id_: str
    velocity_spec: str = ''
    frame_of_reference: str = ''
    vmag: str = ''
    t: str = ''
    ke_spec: str = ''
    turb_intensity: str = ''
    turb_hydraulic_diam: str = ''
    turb_viscosity_ratio: str = ''

    _VELOCITY_SPEC = {
        '1': 'Magnitude and Direction',
        '2': 'Magnitude, Normal to Boundary',
        '3': 'Components',
    }

    _FRAME_OF_REFERENCE = {
        '0': 'Absolute',
        '1': 'Ralative to Adjacent Cell Zone',
    }

    _KE_SPEC = {
        '1': 'Intensity and Length Scale',
        '2': 'Intensith and Viscosity Ratio',
        '3': 'Intensity and Hydraulic Diameter',
    }

    def to_dict(self) -> dict[str, str]:
        data = self.__dict__.copy()

        data['velocity_spec'] = self._VELOCITY_SPEC.get(self.velocity_spec, 'unknown')
        data['frame_of_reference'] = self._FRAME_OF_REFERENCE.get(self.frame_of_reference, 'unknown')
        data['ke_spec'] = self._KE_SPEC.get(self.ke_spec, 'unknown')

        return data


@dataclass
class MassFlowInlet:
    name: str
    id_: str
    mass_flow: str = ''
    t: str = ''
    turb_intensity: str = ''
    turb_hydraulic_diam: str = ''
    turb_viscosity_ratio: str = ''


@dataclass
class MassFlowOutlet:
    name: str
    id_: str
    mass_flow: str = ''
    t: str = ''
    turb_intensity: str = ''
    turb_hydraulic_diam: str = ''
    turb_viscosity_ratio: str = ''


@dataclass
class PressureOutlet:
    name: str
    id_: str
    p: str = ''
    t: str = ''
    ke_spec: str = ''
    prevent_reverse_flow: str = ''
    radial: str = ''
    avg_press_spec: str = ''
    turb_intensity: str = ''
    targeted_mf_boundary: str = ''
    turb_hydraulic_diam: str = ''
    turb_viscosity_ratio: str = ''

    _KE_SPEC = {
        '1': 'Intensity and Length Scale',
        '2': 'Intensith and Viscosity Ratio',
        '3': 'Intensity and Hydraulic Diameter',
    }

    def to_dict(self) -> dict[str, str]:
        data = self.__dict__.copy()

        if self.prevent_reverse_flow == '#t':
            for key in ['t', 'ke_spec', 'turb_intensity', 'targeted_mf_boundary', 'turb_hydraulic_diam', 'turb_viscosity_ratio']:
                data.pop(key, None)

        return data


@dataclass
class Wall:
    name: str
    id_: str
    d: str = ''
    q_dot: str = ''
    material: str = ''
    thermal_bc: str = ''
    t: str = ''
    q: str = ''
    h: str = ''
    motion_bc: str = ''
    shear_bc: str = ''
    rough_bc: str = ''
    moving: str = ''
    relative: str = ''
    roughness_height: str = ''
    roughness_const: str = ''

    _THERMAL_BC = {
        '1': 'Heat Flux',
        '2': 'Temperature',
        '3': 'Coupled',
    }

    _MOTION_BC = {}

    _SHEAR_BC = {}

    _ROUGH_BC = {}

    _THERMAL_BC_WHITELIST = {
        '1': {'q'},  # Heat Flux
        '2': {'t'},  # Temperature
        '3': set(),  # Coupled
    }

    def to_dict(self) -> dict[str, str]:
        data = self.__dict__.copy()

        allowed_attrs = self._THERMAL_BC_WHITELIST.get(self.thermal_bc, set())
        all_thermal_attrs = {'t', 'q', 'h'}
        attrs_to_remove = all_thermal_attrs - allowed_attrs
        for attr in attrs_to_remove:
            data.pop(attr, None)

        data['thermal_bc'] = self._THERMAL_BC.get(self.thermal_bc, 'unknown')

        return data


@dataclass
class PorousJump:
    name: str
    id_: str
    alpha: str = ''
    dm: str = ''
    c2: str = ''


@dataclass
class Fan:
    name: str
    id_: str


@dataclass
class Radiator:
    name: str
    id_: str


@dataclass
class Interior:
    name: str
    id_: str


@dataclass
class Symmetry:
    name: str
    id_: str


@dataclass
class NotImplementedBoundary:
    name: str
    id_: str


class BoundaryFactory:
    @staticmethod
    def create(name: str, id_: str, type_: str):
        match type_:
            case 'fluid':
                return Fluid(name, id_)
            case 'solid':
                return Solid(name, id_)
            case 'velocity-inlet':
                return VelocityInlet(name, id_)
            case 'mass-flow-inlet':
                return MassFlowInlet(name, id_)
            case 'mass-flow-outlet':
                return MassFlowOutlet(name, id_)
            case 'pressure-outlet':
                return PressureOutlet(name, id_)
            case 'wall':
                return Wall(name, id_)
            case 'interior':
                return Interior(name, id_)
            case 'symmetry':
                return Symmetry(name, id_)
            case 'porous-jump':
                return PorousJump(name, id_)
            case _:
                return NotImplementedBoundary(name, id_)
