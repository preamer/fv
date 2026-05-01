""" A python CLI script to change the version of Ansys Fluent .h5 file """


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


def main():
    import argparse

    desc = "A python CLI script to change the version of Ansys Fluent .h5 file"
    parser = argparse.ArgumentParser(description=desc)
    parser.add_argument("file_path", type=str, help="Path to the .h5 file")
    parser.add_argument("-t", "--to", type=str, help="Version to change to, e.g. 25.2")
    args = parser.parse_args()

    if args.to:
        change_version(args.file_path, args.to)
    else:
        print_version(args.file_path)
