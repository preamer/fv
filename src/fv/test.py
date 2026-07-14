import pyvista as pv

import mesh_reader

file_path_msh = "square-10x10-quad.msh.h5"
file_path_cas = "square-10x10-quad.cas.h5"

ret_msh = mesh_reader.read_mesh_data(file_path_msh)
print(ret_msh['cells'][:10])
ret_cas = mesh_reader.read_mesh_data(file_path_cas)
print(ret_cas['cells'][:10])

# ret2 = mesh_reader.read_pyvista_mesh(file_path)
# print(ret2)

# pv.plot(
#     ret2,
#     show_edges=True,
#     show_axes=True,
#     smooth_shading=True,
#     split_sharp_edges=True,
# )
