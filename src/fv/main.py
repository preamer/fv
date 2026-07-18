def print_version(file_path: str) -> str:
    """Get the version of the .h5 file

    Parameters
    ---------
    file_path : str
        Path to the .h5 file

    Returns
    -------
    str
        Version of the .h5 file
    """
    import h5py

    with h5py.File(file_path, "r") as f:
        print(f['/settings/Version'][0].decode())


def change_version(file_path: str, to: str) -> None:
    """Change the version of the .h5 file

    Parameters
    ---------
    file_path : str
        Path to the .h5 file
    to : str
        Version to change to
    """
    import h5py

    with h5py.File(file_path, "r+") as f:
        f['/settings/Version'][0] = to


def read_case(file_path: str, **kwargs) -> dict[str]:
    """Read the cas.h5 file

    Parameters
    ---------
    file_path : str
        Path to the cas.h5 file

    Returns
    -------
    dict[str]
        A dictionary containing the case settings
    """
    import re
    import h5py

    with h5py.File(file_path) as f:
        settings: h5py.Group = f['/settings']
        general_info = settings['Rampant Variables'][0].decode()
        boundary_info = settings['Thread Variables'][0].decode()

    if not any(kwargs.values()):
        kwargs = dict.fromkeys(kwargs.keys(), True)

    data = {}

    if kwargs['solver']:
        case_config = re.search(
            r'^\(case-config.*',
            general_info,
            re.M
        ).group()
        kvs = {
            m[0]: m[1]
            for m in re.findall(
                r"\(([^()\s]+)\s+\.\s+([^()\s]+)\)",
                case_config
            )
        }

        data['solver'] = {}
        data['solver']['type'] = "pbns" if kvs['rp-seg?'] == "#t" else "dbns"
        data['solver']['time'] = "transient" if kvs['rp-unsteady?'] == "#t" else "steady"
        data['solver']['dimension'] = "3d" if kvs['rp-3d?'] == "#t" else "2d"
        data['solver']['precision'] = "double" if kvs['rp-double?'] == "#t" else "single"
        data['solver']['axi'] = "true" if kvs['rp-axi?'] == "#t" else "false"
        data['solver']['init'] = "hybrid" if kvs['hyb-init?'] == "#t" else "standard"

        if kvs['rp-visc?'] == "#f":
            data['solver']['turb'] = "inviscid"
        else:
            for key in [
                'rp-lam?', 'rp-ke?', 'rp-kw?', 'rp-sa?', 'sg-rsm?',
                'rp-les?', 'rp-des?', 'rp-kklw', 'rp-v2f?'
            ]:
                if kvs[key] == "#t":
                    data['solver']['turb'] = key[3:-1]
                    break

        data['solver']['energy'] = "true" if kvs['rf-energy?'] == "#t" else "false"
        data['solver']['radiation'] = "false"
        for key in ['sg-rosseland?', 'sg-p1?', 'sg-dtrm?', 'sg-s2s?', 'sg-disco?']:
            if kvs[key] != "#f":
                data['solver']['radiation'] = key[3:-1]
                break

        gravity = re.search(
            r'\(gravity\?\s+([^)\s]+)\)',
            general_info
        ).group(1)
        if gravity == "#t":
            axes = ['x', 'y', 'z'] if data['solver']['dimension'] == "3d" else ['x', 'y']
            for axis in axes:
                sel = re.search(
                    fr'\(gravity/{axis}-sel\s+"([^"]+)"\)',
                    general_info
                ).group(1)
                expr = re.search(
                    fr'\(gravity/{axis}-expr\s+"([^"]+)"\)',
                    general_info
                ).group(1)
                data['solver'][f'gravity/{axis}'] = f'{sel}/{expr}'
        else:
            data['solver']['gravity'] = "false"

        operating_conditions = [
            'operating-pressure',
            'pressure-reference/x', 'pressure-reference/y', 'pressure-reference/z',
            'operating-temperature',
        ]
        use_operating_density = re.search(
            r'\(use-operating-density\?\s+([^)\s]+)\)',
            general_info
        ).group(1)
        if use_operating_density == '#t':
            operating_conditions.append('operating-density')
        for condition in operating_conditions:
            sel = re.search(
                fr'\({condition}-sel\s+"([^"]+)"\)',
                general_info
            ).group(1)
            expr = re.search(
                fr'\({condition}-expr\s+"([^"]+)"\)',
                general_info
            ).group(1)
            data['solver'][condition] = f'{sel}/{expr}'

    if kwargs['mat']:
        import sexpdata

        data['materials'] = {}
        materials = re.search(
            r'(\(materials.*)',
            general_info,
            re.M
        ).group(1)
        materials: list = sexpdata.loads(materials)
        for material in materials[1]:
            name = str(material[0])
            data['materials'][name] = {}
            data['materials'][name]['type'] = str(material[1])
            for i in range(2, len(material)):
                property_ = material[i]
                if property_[1] == sexpdata.Symbol('.'):
                    data['materials'][name][str(property_[0])] = str(property_[2])
                elif isinstance(p1 := property_[1], list):
                    if p1[1] == sexpdata.Symbol('.'):
                        data['materials'][name][str(property_[0])] = f'{p1[0]}/{p1[2]}'
                    else:
                        value = ' '.join(str(p) for p in p1[1:])
                        data['materials'][name][str(property_[0])] = f'{p1[0]}/{value}'

    if kwargs['bd']:
        import sexpdata
        from .boundary import BoundaryFactory

        data['boundary'] = {}
        boundaries: list[list] = sexpdata.parse(boundary_info, true=None)
        for boundary_info in boundaries:
            id_, type_, name, _ = [str(_) for _ in boundary_info[1]]
            new_boundary = BoundaryFactory.create(name, id_, type_)
            b_list = data['boundary'].get(type_, [])

            for property_ in boundary_info[2]:
                property_name = str(property_[0]).replace('-', '_').replace('?', '').replace('/', '_')
                if hasattr(new_boundary, property_name):
                    if property_[1] == sexpdata.Symbol('.'):
                        setattr(new_boundary, property_name, str(property_[2]))
                    elif isinstance(property_[1], list):
                        setattr(new_boundary, property_name, f'{property_[1][0]}/{property_[1][2]}')

            b_list.append(new_boundary.to_dict() if hasattr(new_boundary, 'to_dict') else new_boundary.__dict__)
            data['boundary'][type_] = b_list

    if kwargs['ne']:
        import sexpdata

        data['ne'] = {}
        nes = re.search(
            r'(\(named-expressions.*)',
            general_info,
            re.M
        ).group(1)
        nes: list = sexpdata.loads(nes, true=None)[1]
        for ne in nes:
            ne_dict = {
                str(property_[0]): str(property_[2])
                for property_ in ne
            }
            data['ne'][ne_dict['name']] = ne_dict

    if kwargs['disc']:
        from .utils import FLUENT_ENUM

        data['disc-scheme'] = {}
        disc_scheme = {
            ds[0]: FLUENT_ENUM[ds[1]]
            for ds in re.findall(
                r'\((.*)/scheme\s+(\d+)\)',
                general_info
            )
        }
        for eq in ['flow', 'pressure', 'mom', 'temperature', 'k', 'omega', 'epsilon']:
            data['disc-scheme'][eq] = disc_scheme.get(eq)

        data['relax-factor'] = {}
        if data['disc-scheme']['flow'] == 'Coupled':
            for eq in ['pressure', 'mom']:
                data['relax-factor'][eq] = re.search(
                    fr'\(pressure-coupled/{eq}/pseudo-explicit-relax\s+([\d.]+)\)',
                    general_info
                ).group(1)
            for eq in ['temperature', 'k', 'omega', 'epsilon', 'turb-viscosity', 'density', 'body-force']:
                data['relax-factor'][eq] = re.search(
                    fr'\({eq}/pseudo-relax\s+([\d.]+)\)',
                    general_info
                ).group(1)
        else:
            ur_factor = {
                ur[0]: ur[1]
                for ur in re.findall(
                    fr'\((.*)/relax\s+([\d.]+)\)',
                    general_info
                )
            }
            for eq in ['pressure', 'mom', 'temperature', 'k', 'omega', 'epsilon', 'turb-viscosity', 'density', 'body-force']:
                data['relax-factor'][eq] = ur_factor.get(eq, '')

    if kwargs['rd']:
        import sexpdata

        data['report-definitions'] = {}
        rds = re.search(
            r'(\(monitor/report-definitions.*)',
            general_info,
            re.M
        ).group(1)
        rds: list = sexpdata.loads(rds, true=None)[1]
        for rd in rds:
            name = str(rd[0][2])
            type_ = str(rd[1][1])
            data['report-definitions'][name] = {'type': type_}
            if 'volume' in type_:
                data['report-definitions'][name]['field'] = str(rd[1][2][2])
                data['report-definitions'][name]['zones'] = [str(zone) for zone in rd[1][6][1:]]
                data['report-definitions'][name]['per-zone?'] = str(rd[1][-5][2])
            elif 'surface' in type_:
                data['report-definitions'][name]['field'] = str(rd[1][2][2])
                data['report-definitions'][name]['surfaces'] = [str(surface) for surface in rd[1][5][1:]]
                data['report-definitions'][name]['per-surface?'] = str(rd[1][-5][2])
            elif 'flux' in type_:
                data['report-definitions'][name]['zones'] = [str(zone) for zone in rd[1][3][1:]]
                data['report-definitions'][name]['per-zone?'] = str(rd[1][-5][2])

    if kwargs['plotsets']:
        import sexpdata

        data['plotsets'] = {}
        plotsets = re.search(
            r'(\(monitor/plotsets.*)',
            general_info,
            re.M
        ).group(1)
        plotsets: list = sexpdata.loads(plotsets, true=None)[1]
        for plotset in plotsets:
            plotset_dict = {
                str(property_[0]): str(property_[2])
                for property_ in plotset
                if str(property_[0]) not in ['old-props', 'report-defs']
            }
            plotset_dict['report-defs'] = plotset[6][1:]
            data['plotsets'][plotset_dict['name']] = plotset_dict

    if kwargs['monitorsets']:
        import sexpdata

        data['monitorsets'] = {}
        monitorsets = re.search(
            r'(\(monitor/monitorsets.*)',
            general_info,
            re.M
        ).group(1)
        monitorsets: list = sexpdata.loads(monitorsets, true=None)[1]
        for monitorset in monitorsets:
            monitorset_dict = {
                str(property_[0]): str(property_[2])
                for property_ in monitorset
                if str(property_[0]) not in ['old-props', 'report-defs']
            }
            monitorset_dict['report-defs'] = monitorset[-4][1:]
            data['monitorsets'][monitorset_dict['name']] = monitorset_dict

    if kwargs['iter']:
        data['iter'] = {}

        if not (solver_time := data.get('solver', {}).get('time', None)):
            case_config = re.search(
                r'^\(case-config.*',
                general_info,
                re.M
            ).group()
            is_unsteady = re.search(
                r"\(rp-unsteady\?\s+\.\s+([^()\s]+)\)",
                case_config
            ).group(1)
            solver_time = "transient" if is_unsteady == "#t" else "steady"

        if solver_time == 'steady':
            data['iter']['iterations'] = re.search(
                r'\(number-of-iterations\s+(\d+)\)',
                general_info
            ).group(1)
        else:
            sel = re.search(
                r'\(physical-time-step-sel\s+"([^"]+)"\)',
                general_info
            ).group(1)
            expr = re.search(
                r'\(physical-time-step-expr\s+"([^"]+)"\)',
                general_info
            ).group(1)
            data['iter']['physical-time-step'] = f'{sel}/{expr}'
            for key in ['time-steps', 'max-iters-per-step', 'time-step', 'flow-time']:
                data['iter'][key] = re.search(
                    fr'\({key}\s+(\d+)\)',
                    general_info
                ).group(1)

    return data


def extract_h5(file_path: str) -> None:
    """Extract cas.h5 general and boundary string to files

    Parameters
    ---------
    file_path : str
        Path to the cas.h5 file
    """
    import h5py
    with h5py.File(file_path) as f:
        settings: h5py.Group = f['/settings']
        general_info = settings['Rampant Variables'][0].decode()
        boundary_info = settings['Thread Variables'][0].decode()
    with open('general.scm', 'w', encoding='utf-8') as f:
        f.write(general_info)
    with open('boundary.scm', 'w', encoding='utf-8') as f:
        f.write(boundary_info)


def main() -> None:
    import argparse

    desc = "A Python CLI tool to get case settings and display mesh without opening Ansys Fluent"
    parser = argparse.ArgumentParser(description=desc)

    parser.add_argument(
        "file_path",
        type=str,
        help="path to the .h5 file"
    )

    parser.add_argument(
        "--extract",
        action="store_true",
        help="extract cas.h5 general and boundary string to files"
    )

    parser.add_argument(
        "-v", "--version",
        action="store_true",
        help="show the version of the .h5 file"
    )
    parser.add_argument(
        "-t", "--to",
        type=str,
        help="version to change to, e.g. 25.2"
    )

    parser.add_argument(
        "--solver",
        action="store_true",
        help="show solver settings"
    )
    parser.add_argument(
        "--mat", "--materials",
        action="store_true",
        help="show materials settings"
    )
    parser.add_argument(
        "--bd", "--boundary",
        action="store_true",
        help="show boundary settings"
    )
    parser.add_argument(
        "--ne", "--named-expressions",
        action="store_true",
        help="show named-expressions settings"
    )
    parser.add_argument(
        "--disc",
        action="store_true",
        help="show disc-scheme and relax-factor settings"
    )
    parser.add_argument(
        "--rd", "--report-definitions",
        action="store_true",
        help="show report-definitions settings"
    )
    parser.add_argument(
        "--plotsets",
        action="store_true",
        help="show report-definitions plotsets settings"
    )
    parser.add_argument(
        "--monitorsets",
        action="store_true",
        help="show report-definitions monitorsets settings"
    )
    parser.add_argument(
        "--iter",
        action="store_true",
        help="show iteration settings"
    )
    parser.add_argument(
        "--save",
        action="store_true",
        help="save output to file"
    )
    parser.add_argument(
        "--showmesh",
        action="store_true",
        help="show mesh using pyvista"
    )

    args = parser.parse_args()

    if args.version:
        if args.to:
            change_version(args.file_path, args.to)
        else:
            print_version(args.file_path)
    elif args.extract:
        extract_h5(args.file_path)
    elif args.file_path.endswith(".msh.h5"):
        ...
    elif args.file_path.endswith(".cas.h5"):
        if args.showmesh:
            import pyvista as pv
            try:
                pv.plot(
                    pv.read(args.file_path, progress_bar=True),
                    show_edges=True,
                    show_axes=True,
                    smooth_shading=True,
                    split_sharp_edges=True,
                )
            except Exception as e:
                print(e)
        else:
            from .utils import print_colored_dict
            kwargs = {
                'solver': args.solver,
                'mat': args.mat,
                'bd': args.bd,
                'ne': args.ne,
                'disc': args.disc,
                'rd': args.rd,
                'plotsets': args.plotsets,
                'monitorsets': args.monitorsets,
                'iter': args.iter
            }
            output = read_case(args.file_path, **kwargs)
            print_colored_dict(output)

            if args.save:
                import json
                with open(f"{args.file_path}.json", "w", encoding="utf-8") as f:
                    json.dump(output, f, ensure_ascii=False, indent=4)
