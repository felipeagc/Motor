# Motor

![Github actions](https://github.com/felipeagc/motor/workflows/build/badge.svg)

Motor is a vulkan game engine implemented in C99.

No screenshots yet, because there's nothing to show.

## Implemented features
- Abstract rendering API (possibility to support multiple graphics APIs through a single interface)
- Custom config file format
- Flexible control over memory allocation
- On the fly GLSL compilation (through shaderc)
- Asset hot-reloading
- GLTF Model loading
- IBL cubemap generation
- PBR

## Not yet implemented
- Audio
- Physics
- Entity system
- Shadow mapping

## Rendering API
The engine uses an rendering API described in [renderer.h](https://github.com/felipeagc/motor/blob/master/include/motor/graphics/renderer.h).
It abstracts vulkan details such as descriptor set allocation, pipeline creation, scratch buffer allocation.

The vulkan implementation of the API is located [here](https://github.com/felipeagc/motor/tree/master/src/motor/graphics/vulkan).
The API is implemented in only 2 `.c` files (excluding the libraries it uses).
Those `.c` files include the `.inl` files, which are just a way to separate the implementation of specific systems into their own files.

It should be pretty simple to see how things are done, the renderer is pretty self-contained,
except for the use of data structures like MtHashMap and the dynamic arrays in array.h.
