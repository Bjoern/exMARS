from setuptools import Extension, setup
import sys
setup(name="exmars", version="0.1.0", py_modules=["Warrior"],
      ext_modules=[Extension("Mars", ["python_mars.c", "pmars.c", "sim.c", "pspace.c"],
                             extra_compile_args=["/std:c17", "/O2"] if sys.platform == "win32" else ["-std=gnu17", "-O3"])])
