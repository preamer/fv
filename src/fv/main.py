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
    """Read the case file

    Parameters
    ---------
    file_path : str
        Path to the .h5 file

    Returns
    -------
    dict[str]
        A dictionary containing the case settings
    """
    import re
    import h5py

    with h5py.File(file_path, "r") as f:
        settings: h5py.Group = f['/settings']
        general_info = settings['Rampant Variables'][0].decode()
        boundary_info = settings['Thread Variables'][0].decode()

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
                m = material[i]
                if m[1] == sexpdata.Symbol('.'):
                    data['materials'][name][str(m[0])] = str(m[2])
                elif isinstance(m[1], list):
                    data['materials'][name][str(m[0])] = f'{m[1][0]}/{m[1][2]}'

    if kwargs['boundary']:
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

            b_list.append(new_boundary.__dict__)
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

    if kwargs['ur']:
        data['ur-factor'] = {}
        ur_factor = {
            ur[0]: ur[1]
            for ur in re.findall(
                r'\((.*)/relax\s+([\d.]+)\)',
                general_info
            )
        }
        for eq in ['flow', 'pressure', 'mom', 'temperature', 'k', 'omega', 'epsilon']:
            data['ur-factor'][eq] = ur_factor.get(eq, '')

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

    if kwargs['iter']:
        data['iter'] = {}
        if data['solver']['time'] == 'steady':
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
            data['iter']['time-steps'] = re.search(
                r'\(number-of-time-steps\s+(\d+)\)',
                general_info
            ).group(1)
            data['iter']['max-iters-per-step'] = re.search(
                r'\(max-iterations-per-step\s+(\d+)\)',
                general_info
            ).group(1)
            data['iter']['time-step'] = re.search(
                r'\(time-step\s+(\d+)\)',
                general_info
            ).group(1)
            data['iter']['flow-time'] = re.search(
                r'\(flow-time\s+(\d+)\)',
                general_info
            ).group(1)

    return data


def extract_h5(file_path: str):
    """ 

    """
    import h5py
    with h5py.File(file_path, "r") as f:
        settings: h5py.Group = f['/settings']
        general_info = settings['Rampant Variables'][0].decode()
        boundary_info = settings['Thread Variables'][0].decode()
    with open('general.scm', 'w') as f:
        f.write(general_info)
    with open('boundary.scm', 'w') as f:
        f.write(boundary_info)


def main():
    import argparse

    desc = "A python CLI tool to handle Ansys Fluent .h5 file"
    parser = argparse.ArgumentParser(description=desc)

    parser.add_argument(
        "file_path",
        type=str,
        help="Path to the .h5 file"
    )

    parser.add_argument(
        "--extract",
        action="store_true",
        help="extract h5 string to file"
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
        "--boundary",
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
        help="show disc-scheme settings"
    )
    parser.add_argument(
        "--ur",
        action="store_true",
        help="show ur-factor settings"
    )
    parser.add_argument(
        "--rd", "--report-definations",
        action="store_true",
        help="show report-definations settings"
    )
    parser.add_argument(
        "--iter",
        action="store_true",
        help="show iteration settings"
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
                pv.plot(pv.read(args.file_path), show_edges=True)
            except Exception as e:
                print(e)
        else:
            from rich import print
            kwargs = {
                'solver': args.solver,
                'mat': args.mat,
                'boundary': args.boundary,
                'ne': args.ne,
                'disc': args.disc,
                'ur': args.ur,
                'rd': args.rd,
                'iter': args.iter
            }
            print(read_case(args.file_path, **kwargs))
