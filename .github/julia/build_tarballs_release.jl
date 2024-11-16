using BinaryBuilder, Pkg

haskey(ENV, "UNO_RELEASE") || error("The environment variable UNO_RELEASE is not defined.")
haskey(ENV, "UNO_COMMIT") || error("The environment variable UNO_COMMIT is not defined.")
haskey(ENV, "UNO_URL") || error("The environment variable UNO_URL is not defined.")

name = "Uno"
version = VersionNumber(ENV["UNO_RELEASE"])

# Collection of sources required to complete build
sources = [
    GitSource(ENV["UNO_URL"], ENV["UNO_COMMIT"]),
    ArchiveSource("https://mumps-solver.org/MUMPS_5.7.3.tar.gz",
                  "84a47f7c4231b9efdf4d4f631a2cae2bdd9adeaabc088261d15af040143ed112"),
    ArchiveSource("https://github.com/phracker/MacOSX-SDKs/releases/download/10.15/MacOSX10.15.sdk.tar.xz",
                  "2408d07df7f324d3beea818585a6d990ba99587c218a3969f924dfcc4de93b62"),
]

# Bash recipe for building across all platforms
script = raw"""
# Export dependencies
mkdir ${prefix}/deps
cd ${libdir}
for file in $(ls .); do
   if [[ -f $file ]]; then
      if [[ -z $(ls -la $file | grep 'artifacts') ]]; then
         cp -P ${file} ${prefix}/deps/${file}
      else
         cp -L ${file} ${prefix}/deps/${file}
      fi
   fi
done
cd ${prefix}
cp -rL share/licenses deps/licenses
chmod -R u=rwx deps
tar -czvf deps.tar.gz deps
rm -r deps

# Compile MUMPS
mkdir -p ${libdir}
cd $WORKSPACE/srcdir/MUMPS*

makefile="Makefile.G95.SEQ"
cp Make.inc/${makefile} Makefile.inc

# Add `-fallow-argument-mismatch` if supported
: >empty.f
FFLAGS=()
if gfortran -c -fallow-argument-mismatch empty.f >/dev/null 2>&1; then
    FFLAGS+=("-fallow-argument-mismatch")
fi
rm -f empty.*

if [[ "${target}" == *apple* ]]; then
    SONAME="-install_name"
else
    SONAME="-soname"
fi

make_args+=(OPTF="-O3"
            OPTL="-O3"
            OPTC="-O3"
            CDEFS=-DAdd_
            LMETISDIR=${libdir}
            IMETIS=-I${includedir}
            LMETIS="-L${libdir} -lmetis"
            ORDERINGSF="-Dpord -Dmetis"
            LIBEXT_SHARED=".${dlext}"
            SHARED_OPT="-shared"
            SONAME="${SONAME}"
            CC="$CC ${CFLAGS[@]}"
            FC="gfortran ${FFLAGS[@]}"
            FL="gfortran"
            RANLIB="echo"
            LIBBLAS="-L${libdir} -lopenblas"
            LAPACK="-L${libdir} -lopenblas")

make -j${nproc} dshared "${make_args[@]}"

mkdir ${includedir}/libseq
cp include/*.h ${includedir}
cp libseq/*.h ${includedir}/libseq
cp lib/*.${dlext} ${libdir}

# Compile Uno
cd $WORKSPACE/srcdir/Uno
mkdir -p build
cd build

if [[ "${target}" == x86_64-apple-darwin* ]]; then
    # Work around the issue
    #     /workspace/srcdir/Uno/uno/options/Presets.cpp:17:48: error: 'value' is unavailable: introduced in macOS 10.14
    #           Presets::set(options, optional_preset.value());
    #                               ^
    #     /opt/x86_64-apple-darwin14/x86_64-apple-darwin14/sys-root/usr/include/c++/v1/optional:938:33: note: 'value' has been explicitly marked unavailable here
    #           constexpr value_type const& value() const&
    #                               ^
    export MACOSX_DEPLOYMENT_TARGET=10.15
    # ...and install a newer SDK which supports `std::filesystem`
    pushd $WORKSPACE/srcdir/MacOSX10.*.sdk
    rm -rf /opt/${target}/${target}/sys-root/System
    cp -ra usr/* "/opt/${target}/${target}/sys-root/usr/."
    cp -ra System "/opt/${target}/${target}/sys-root/."
    popd
fi

if [[ "${target}" == *mingw* ]]; then
    LIBHIGHS=${prefix}/lib/libhighs.dll.a
else
    LIBHIGHS=${libdir}/libhighs.${dlext}
fi

if [[ "${target}" == *apple* ]] || [[ "${target}" == *freebsd* ]]; then
    OMP=omp
else
    OMP=gomp
fi

# FortranCInterface_VERIFY fails on macOS, but it's not actually needed for the current build
sed -i 's/FortranCInterface_VERIFY(CXX)/# FortranCInterface_VERIFY(CXX)/g' ../CMakeLists.txt

cmake \
    -DCMAKE_INSTALL_PREFIX=${prefix} \
    -DCMAKE_PREFIX_PATH=${libdir} \
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TARGET_TOOLCHAIN} \
    -DCMAKE_BUILD_TYPE=Release \
    -DAMPLSOLVER=${libdir}/libasl.${dlext} \
    -DHIGHS=${LIBHIGHS} \
    -DHSL=${libdir}/libhsl.${dlext} \
    -DMUMPS_INCLUDE_DIR=${includedir} \
    -DMETIS_INCLUDE_DIR=${includedir} \
    -DMUMPS_LIBRARY="${libdir}/libdmumps.${dlext}" \
    -DMUMPS_COMMON_LIBRARY="${libdir}/libmumps_common.${dlext}" \
    -DMUMPS_PORD_LIBRARY="${libdir}/libpord.${dlext}" \
    -DMUMPS_MPISEQ_LIBRARY="${libdir}/libmpiseq.${dlext}" \
    -DBLAS_LIBRARIES="${libdir}/libopenblas.${dlext}" \
    -DLAPACK_LIBRARIES="${libdir}/libopenblas.${dlext}" \
    ..

make -j${nproc}

# Uno does not support `make install`. Manually copy for now.
install -v -m 755 "uno_ampl${exeext}" -t "${bindir}"

# Currently, Uno does not provide a shared library. This may be useful in the future once it has a C API.
# We just check that we can generate it, but we don't include it in the tarballs.
${CXX} -shared $(flagon -Wl,--whole-archive) libuno.a $(flagon -Wl,--no-whole-archive) -o libuno.${dlext} -L${libdir} -l${OMP} -lopenblas -ldmumps -lmetis -lhsl -lhighs
cp libuno.a ${prefix}/lib/libuno.a
cp libuno.${dlext} ${libdir}/libuno.${dlext}
"""

# These are the platforms we will build for by default, unless further
# platforms are passed in on the command line
platforms = supported_platforms()
platforms = expand_gfortran_versions(platforms)

# The products that we will ensure are always built
products = [
   LibraryProduct("libdmumps", :libdmumps),
   ExecutableProduct("uno_ampl", :amplexe),
   LibraryProduct("libuno", :libuno),
   FileProduct("lib/libuno.a", :libuno_a),
]

# Dependencies that must be installed before this package can be built
dependencies = [
    Dependency(PackageSpec(name="HiGHS_jll", uuid="8fd58aa0-07eb-5a78-9b36-339c94fd15ea")),
    Dependency(PackageSpec(name="HSL_jll", uuid="017b0a0e-03f4-516a-9b91-836bbd1904dd")),
    Dependency(PackageSpec(name="METIS_jll", uuid="d00139f3-1899-568f-a2f0-47f597d42d70")),
    Dependency(PackageSpec(name="ASL_jll", uuid="ae81ac8f-d209-56e5-92de-9978fef736f9"), compat="0.1.3"),
    Dependency(PackageSpec(name="OpenBLAS32_jll", uuid="656ef2d0-ae68-5445-9ca0-591084a874a2")),
    # For OpenMP we use libomp from `LLVMOpenMP_jll` where we use LLVM as compiler (BSD systems),
    # and libgomp from `CompilerSupportLibraries_jll` everywhere else.
    Dependency(PackageSpec(name="CompilerSupportLibraries_jll", uuid="e66e0078-7015-5450-92f7-15fbd957f2ae"); platforms=filter(!Sys.isbsd, platforms)),
    Dependency(PackageSpec(name="LLVMOpenMP_jll", uuid="1d63c593-3942-5779-bab2-d838dc0a180e"); platforms=filter(Sys.isbsd, platforms)),
    HostBuildDependency(PackageSpec(name="CMake_jll", uuid="3f4e10e2-61f2-5801-8945-23b9d642d0e6")),
]

build_tarballs(
    ARGS,
    name,
    version,
    sources,
    script,
    platforms,
    products,
    dependencies;
    julia_compat = "1.6",
    preferred_gcc_version = v"10.2.0",
)