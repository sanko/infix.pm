package Affix 1.00 {    # 'FFI' is my middle name!

    #~ G|-----------------------------------|-----------------------------------||
    #~ D|--------------------------4---5~---|--4--------------------------------||
    #~ A|--7~\-----4---44-/777--------------|------7/4~-------------------------||
    #~ E|-----------------------------------|-----------------------------------||
    use v5.40;
    use vars               qw[@EXPORT_OK @EXPORT %EXPORT_TAGS];
    use warnings::register qw[Type];
    use feature            qw[class];
    no warnings qw[experimental::class experimental::try];
    use Carp                  qw[];
    use Config                qw[%Config];
    use File::Spec::Functions qw[rel2abs canonpath curdir path catdir];
    use File::Basename        qw[basename dirname];
    use File::Find            qw[find];
    use File::Temp            qw[tempdir];
    #
    my $okay = 0;
    #
    BEGIN {
        use XSLoader;
        $DynaLoad::dl_debug = 1;
        $okay               = XSLoader::load();
        my $platform
            = 'Affix::Platform::' .
            ( ( $^O eq 'MSWin32' ) ? 'Windows' :
                $^O eq 'darwin'                                                                   ? 'MacOS' :
                ( $^O eq 'freebsd' || $^O eq 'openbsd' || $^O eq 'netbsd' || $^O eq 'dragonfly' ) ? 'BSD' :
                'Unix' );

        #~ warn $platform;
        #~ use base $platform;
        eval 'use ' . $platform . ' qw[:all];';
        $@ && die $@;
        our @ISA = ($platform);
    }
    $EXPORT_TAGS{pin}    = [qw[pin unpin]];
    $EXPORT_TAGS{memory} = [
        qw[ affix wrap pin unpin
            cast
            errno getwinerror
            malloc calloc realloc free
            memchr memcmp memset memcpy memmove
            sizeof offsetof alignof
            raw hexdump],
    ];
    $EXPORT_TAGS{lib}   = [qw[load_library find_library find_symbol dlerror libm libc]];
    $EXPORT_TAGS{types} = [
        qw[         Void Bool
            Char UChar SChar WChar
            Short UShort
            Int UInt
            Long ULong
            LongLong ULongLong
            Float Double LongDouble
            SInt8 UInt8 SInt16 UInt16 SInt32 UInt32 SInt64 UInt64 SInt128 UInt128
            Float32 Float64
            Size_t SSize_t
            String WString
            Pointer Array Struct Union Enum Callback CodeRef Complex Vector
            Packed VarArgs
            SV
            M256 M256d M512 M512d M512i
        ]
    ];
    {
        my %seen;
        push @{ $EXPORT_TAGS{default} }, grep { !$seen{$_}++ } @{ $EXPORT_TAGS{$_} } for qw[core types cc lib];
    }
    {
        my %seen;
        push @{ $EXPORT_TAGS{all} }, grep { !$seen{$_}++ } @{ $EXPORT_TAGS{$_} } for keys %EXPORT_TAGS;
    }
    #
    @EXPORT    = sort @{ $EXPORT_TAGS{default} };    # XXX: Don't do this...
    @EXPORT_OK = sort @{ $EXPORT_TAGS{all} };
    #
    sub libm() { CORE::state $m //= find_library('m'); $m }
    sub libc() { CORE::state $c //= find_library('c'); $c }
    #
    our $OS = $^O;
    my $is_win = $OS eq 'MSWin32';
    my $is_mac = $OS eq 'darwin';
    my $is_bsd = $OS =~ /bsd/;
    my $is_sun = $OS =~ /(solaris|sunos)/;
    #
    sub locate_libs ( $lib, $version ) {
        $lib =~ s[^lib][];
        my $ver;
        if ( defined $version ) {
            require version;
            $ver = version->parse($version);
        }

        #~ warn $lib;
        #~ warn $version;
        #~ warn "Win: $is_win";
        #~ warn "Mac: $is_mac";
        #~ warn "BSD: $is_bsd";
        #~ warn "Sun: $is_sun";
        CORE::state $libdirs;
        if ( !defined $libdirs ) {
            if ($is_win) {
                require Win32;
                $libdirs = [ Win32::GetFolderPath( Win32::CSIDL_SYSTEM() ) . '/', Win32::GetFolderPath( Win32::CSIDL_WINDOWS() ) . '/', ];
            }
            else {
                $libdirs = [
                    ( split ' ', $Config{libsdirs} ),
                    map { warn $ENV{$_}; split /[:;]/, ( $ENV{$_} ) }
                        grep { $ENV{$_} } qw[LD_LIBRARY_PATH DYLD_LIBRARY_PATH DYLD_FALLBACK_LIBRARY_PATH]
                ];
            }
            no warnings qw[once];
            require DynaLoader;
            $libdirs = [
                grep { -d $_ } map { rel2abs($_) } qw[. ./lib ~/lib /usr/local/lib /usr/lib /lib /usr/lib/system], @DynaLoader::dl_library_path,
                @$libdirs
            ];
        }
        CORE::state $regex;
        if ( !defined $regex ) {
            $regex = $is_win ?
                qr/^
        (?:lib)?(?<name>\w+)
        (?:[_-](?<version>[0-9\-\._]+))?_*
        \.$Config{so}
        $/ix :
                $is_mac ?
                qr/^
        (?:lib)?(?<name>\w+)
        (?:\.(?<version>[0-9]+(?:\.[0-9]+)*))?
        \.(?:so|dylib|bundle)
        $/x :    # assume *BSD or linux
                qr/^
        (?:lib)?(?<name>\w+)
        \.$Config{so}
        (?:\.(?<version>[0-9]+(?:\.[0-9]+)*))?
        $/x;
        }
        my %store;

        #~ warn join ', ', @$libdirs;
        my %_seen;
        find(
            0 ?
                sub {    # This is rather slow...
                warn $File::Find::name;
                return if $store{ basename $File::Find::name };

                #~ return if $_seen{basename $File::Find::name}++;
                return if !-e $File::Find::name;
                warn basename $File::Find::name;
                warn;
                $File::Find::prune = 1 if !grep { canonpath $_ eq canonpath $File::Find::name } @$libdirs;
                /$regex/ or return;
                warn;
                $+{name} eq $lib or return;
                warn;
                my $lib_ver;
                $lib_ver = version->parse( $+{version} ) if defined $+{version};
                $store{ canonpath $File::Find::name } = { %+, path => $File::Find::name, ( defined $lib_ver ? ( version => $lib_ver ) : () ) }
                    if ( defined($ver) && defined($lib_ver) ? $lib_ver == $ver : 1 );
                } :
                sub {
                $File::Find::prune = 1 if !grep { canonpath $_ eq canonpath $File::Find::name } @$libdirs;

                #~ return                 if -d $_;
                return unless $_ =~ $regex;
                return unless defined $+{name};
                return unless $+{name} eq $lib;
                return unless -B $File::Find::name;
                my $lib_ver;
                $lib_ver = version->parse( $+{version} ) if defined $+{version};
                return unless ( defined $lib_ver && defined($ver) ? $ver == $lib_ver : 1 );

                #~ use Data::Dump;
                #~ warn $File::Find::name;
                #~ ddx %+;
                $store{ canonpath $File::Find::name } //= { %+, path => $File::Find::name, ( defined $lib_ver ? ( version => $lib_ver ) : () ) };
                },
            @$libdirs
        );
        values %store;
    }

    sub locate_lib( $name, $version ) {
        return $name if $name && -B $name;
        CORE::state $cache //= {};
        return $cache->{$name}{ $version // '' }->{path} if defined $cache->{$name}{ $version // '' };
        if ( !$version ) {
            return $cache->{$name}{''}{path} = rel2abs($name)                       if -B rel2abs($name);
            return $cache->{$name}{''}{path} = rel2abs( $name . '.' . $Config{so} ) if -B rel2abs( $name . '.' . $Config{so} );
        }
        my $libname = basename $name;
        $libname =~ s/^lib//;
        $libname =~ s/\..*$//;
        return $cache->{$libname}{ $version // '' }->{path} if defined $cache->{$libname}{ $version // '' };
        my @libs = locate_libs( $name, $version );

        #~ warn;
        #~ use Data::Dump;
        #~ warn join ', ', @_;
        #~ ddx \@_;
        #~ ddx $cache;
        if (@libs) {
            ( $cache->{$name}{ $version // '' } ) = @libs;
            return $cache->{$name}{ $version // '' }->{path};
        }
        ();
    }

    # Regex to heuristically identify if a string is a valid infix type signature.
    # Matches primitives, pointers (*), arrays ([), structs ({), unions (<), named types (@), etc.
    my $IS_TYPE
        = qr/^(?:void|bool|[us]?char|u?short|u?int|u?long(?:long)?|float|double|longdouble|s?size_t|s?int\d+|uint\d+|float\d+|m\d+[a-z]*|e:|c\[|v\[|\*|\[|\{|\!|<|\(|@)/;

    # Abstract
    sub Void ()       {'void'}
    sub Bool ()       {'bool'}
    sub Char ()       {'char'}
    sub UChar()       {'uchar'}
    sub SChar()       {'char'}
    sub Short ()      {'short'}
    sub UShort ()     {'ushort'}
    sub Int ()        {'int'}
    sub UInt ()       {'uint'}
    sub Long ()       {'long'}
    sub ULong ()      {'ulong'}
    sub LongLong ()   {'longlong'}
    sub ULongLong ()  {'ulonglong'}
    sub Float ()      {'float'}
    sub Double ()     {'double'}
    sub LongDouble () {'longdouble'}
    sub Size_t ()     {'size_t'}
    sub SSize_t ()    {'ssize_t'}

    # Fixed-width
    sub SInt8()    {'sint8'}
    sub UInt8()    {'uint8'}
    sub SInt16()   {'sint16'}
    sub UInt16()   {'uint16'}
    sub SInt32()   {'sint32'}
    sub UInt32()   {'uint32'}
    sub SInt64()   {'sint64'}
    sub UInt64()   {'uint64'}
    sub SInt128()  {'sint128'}
    sub UInt128()  {'uint128'}
    sub Float32()  {'float32'}
    sub Float64 () {'float64'}
    sub Char8()    {'char8_t'}
    sub Char16()   {'char16_t'}
    sub Char32()   {'char32_t'}

    # SIMD aliases
    sub M256 ()  {'m256'}
    sub M256d () {'m256d'}
    sub M512 ()  {'m512'}
    sub M512d () {'m512d'}
    sub M512i () {'m512i'}

    # Composites
    sub Pointer : prototype($) {
        my $type = ref( $_[0] ) ? $_[0]->[0] : $_[0];
        return '*' . ( $type // Void() );
    }

    # Struct[ id => Int, score => Double ] -> {id:int,score:double}
    sub Struct : prototype($) { _build_aggregate( $_[0], '{%s}' ) }

    # Union[ i => Int, f => Float ] -> <i:int,f:float>
    sub Union : prototype($) { _build_aggregate( $_[0], '<%s>' ) }

    sub Array : prototype($) {
        my ( $type, $size ) = @{ $_[0] };
        return "[$size:$type]";
    }

    # Callback[ [Int, Int] => Void ] -> (int,int)->void
    # Callback[ [String, VarArgs, Int] => Void ] -> (*char;int)->void
    sub Callback : prototype($) {
        my $args   = $_[0];
        my $params = $args->[0];    # Array ref of types
        my $ret    = $args->[1];    # Return type

        # Handle VarArgs marker ';'
        my $joined = join( ',', @$params );

        # Correctly format the semicolon: "int,;,double" -> "int;double"
        $joined =~ s/,\;,/;/g;
        $joined =~ s/;/,/;          # Fallback if it's at start/end/weird spot, signature parser handles it?

        # Actually signature.md says: (fixed; variadic)
        # So we want literally "int,int;double"
        $joined =~ s/,\;$/;/;    # Trailing VarArgs
        return "*(($joined)->$ret)";
    }

    # Complex[ Double ] -> c[double]
    sub Complex : prototype($) {
        my $type = ref( $_[0] ) ? $_[0]->[0] : $_[0];
        return "c[$type]";
    }

    # Vector[ 4, Float ] -> v[4:float]
    sub Vector : prototype($) {
        my ( $size, $type ) = @{ $_[0] };
        return "v[$size:$type]";
    }

    # Enum[ Int ] -> e:int
    # Enum[ [ K=>V, ... ], Int ] -> e:int (We ignore the values for the signature)
    sub Enum : prototype($) {
        my $args = $_[0];
        my $type = ( ref($args) eq 'ARRAY' && @$args > 1 ) ? $args->[1] : 'int';
        return "e:$type";
    }

    # Packed[ Struct[...] ]        -> !{...}
    # Packed( 4, [ Struct[...] ] ) -> !4:{...}
    sub Packed : prototype($) {

        # If called as Packed(4, [...])
        if ( @_ == 2 && !ref( $_[0] ) ) {
            my ( $align, $content ) = @_;

            # If content is already wrapped in {}, strip them?
            # No, signatures.md says !N:{...}. Struct returns {...}.
            # So we just prepend !N:
            return "!$align:" . _build_aggregate( $content, '{%s}' );
        }

        # If called as Packed([ ... ])
        my $content = $_[0];
        return '!' . _build_aggregate( $content, '{%s}' );
    }

    # Special marker for Variadic functions
    sub VarArgs () {';'}

    # Semantic aliases and convienient types
    sub String ()  {'*char'}
    sub WString () {'*ushort'}
    sub SV()       {'SV'}

    # Helper for Struct/Union to handle "Name => Type" syntax
    sub _build_aggregate {
        my ( $args, $wrapper ) = @_;
        my @parts;
        for ( my $i = 0; $i < @$args; $i++ ) {
            my $curr = $args->[$i];
            my $next = $args->[ $i + 1 ];

            # Heuristic: If current is NOT a type, and next IS a type, treat as Key => Value
            if ( defined $next && $curr !~ $IS_TYPE && $next =~ $IS_TYPE ) {
                push @parts, "$curr:$next";
                $i++;    # Skip the type
            }
            else {
                # Anonymous member
                push @parts, $curr;
            }
        }
        my $content = join( ',', @parts );
        return sprintf( $wrapper, $content );
    }

    # Demo lib builder
    {

        class Affix::Compiler {
            use Config     qw[%Config];
            use Path::Tiny qw[path tempdir];
            use File::Spec;
            use ExtUtils::MakeMaker;
            #
            field $os        : param : reader //= $^O;
            field $cleanup   : param : reader //= 0;
            field $version   : param : reader //= ();
            field $build_dir : param : reader //= tempdir( CLEANUP => $cleanup );
            field $name      : param : reader;
            field $libname : reader
                = $build_dir->child( ( ( $os eq 'MSWin32' || $name =~ /^lib/ ) ? '' : 'lib' ) .
                    $name . '.' .
                    $Config{so} .
                    ( $os eq 'MSWin32' || !defined $version ? '' : '.' . $version ) )->absolute;
            field $platform : reader = ();    # ADJUST
            field $source   : param : reader;
            field $flags    : param : reader //= {

                #~ ldflags => $Config{ldflags},
                cflags   => $Config{cflags},
                cppflags => $Config{cxxflags}
            };
            field @objs : reader = [];
            ADJUST {
                $source = [ map { _filemap($_) } @$source ];
            }
            #
            sub _can_run(@cmd) {
                state $paths //= [ map { $_->realpath } grep { $_->exists } map { path($_) } File::Spec->path ];
                for my $exe (@cmd) {
                    grep { return path($_) if $_ = MM->maybe_command($_) } $exe, map { $_->child($exe) } @$paths;
                }
            }
            #
            field $linker : reader : param //= _can_run qw[g++ ld];

            #~ https://gcc.gnu.org/onlinedocs/gcc-3.4.0/gnat_ug_unx/Creating-an-Ada-Library.html
            field $ada : reader : param //= _can_run qw[gnatmake];

            #~ https://fasterthanli.me/series/making-our-own-executable-packer/part-5
            #~ https://stackoverflow.com/questions/71704813/writing-and-linking-shared-libraries-in-assembly-32-bit
            #~ https://github.com/therealdreg/nasm_linux_x86_64_pure_sharedlib
            field $asm : reader : param //= _can_run qw[nasm as];
            field $c   : reader : param //= _can_run qw[gcc clang cc icc icpx cl eccp];
            field $cpp : reader : param //= _can_run qw[g++ clang++ c++ icpc icpx cl eccp];

            #~ https://c3-lang.org/build-your-project/build-commands/
            field $c3 : reader : param //= _can_run qw[c3c];

            #~ https://www.circle-lang.org/site/index.html
            field $circle : reader : param //= _can_run qw[circle];

            #~ https://mazeez.dev/posts/writing-native-libraries-in-csharp
            #~ https://medium.com/@sixpeteunder/how-to-build-a-shared-library-in-c-sharp-and-call-it-from-java-code-6931260d01e5
            field $csharp : reader : param //= _can_run qw[dotnet];

            # cobc: https://gnucobol.sourceforge.io/
            field $cobol : reader : param //= _can_run qw[cobc cobol cob cob2];

            #~ https://github.com/crystal-lang/crystal/issues/921#issuecomment-2413541412
            field $crystal : reader : param //= _can_run qw[crystal];

            #~ https://wiki.liberty-eiffel.org/index.php/Compile
            #~ https://svn.eiffel.com/eiffelstudio-public/branches/Eiffel_54/Delivery/docs/papers/dll.html
            field $eiffel : reader : param //= _can_run qw[se];

            #~ https://dlang.org/articles/dll-linux.html#dso9
            #~ dmd -c dll.d -fPIC
            #~ dmd -oflibdll.so dll.o -shared -defaultlib=libphobos2.so -L-rpath=/path/to/where/shared/library/is
            field $d       : reader : param //= _can_run qw[dmd];
            field $fortran : reader : param //= _can_run qw[gfortran ifx ifort];

            #~ https://github.com/secana/Native-FSharp-Library
            #~ https://secanablog.wordpress.com/2020/02/01/writing-a-native-library-in-f-which-can-be-called-from-c/
            field $fsharp : reader : param //= _can_run qw[dotnet];

            #~ https://futhark.readthedocs.io/en/stable/usage.html
            field $futhark : reader : param //= _can_run qw[futhark];    # .fut => .c

            #~ https://medium.com/@walkert/fun-building-shared-libraries-in-go-639500a6a669
            #~ https://github.com/vladimirvivien/go-cshared-examples
            field $go : reader : param //= _can_run qw[go];

            #~ https://github.com/bennoleslie/haskell-shared-example
            #~ https://www.hobson.space/posts/haskell-foreign-library/
            field $haskell : reader : param //= _can_run qw[ghc cabal];

            #~ https://peterme.net/dynamic-libraries-in-nim.html
            field $nim : reader : param //= _can_run qw[nim];    # .nim => .c

            #~ https://odin-lang.org/news/calling-odin-from-python/
            field $odin : reader : param //= _can_run qw[odin];

            #~ https://p-org.github.io/P/getstarted/install/#step-4-recommended-ide-optional
            #~ https://p-org.github.io/P/getstarted/usingP/#compiling-a-p-program
            field $p : reader : param //= _can_run qw[p];    # .p => C#

            #~ https://blog.asleson.org/2021/02/23/how-to-writing-a-c-shared-library-in-rust/
            field $rust : reader : param //= _can_run qw[cargo];

            #~ swiftc point.swift -emit-module -emit-library
            #~ https://forums.swift.org/t/creating-a-c-accessible-shared-library-in-swift/45329/5
            #~ https://theswiftdev.com/building-static-and-dynamic-swift-libraries-using-the-swift-compiler/#should-i-choose-dynamic-or-static-linking
            field $swift : reader : param //= _can_run qw[swiftc];

            #~ https://www.rangakrish.com/index.php/2023/04/02/building-v-language-dll/
            #~ https://dev.to/piterweb/how-to-create-and-use-dlls-on-vlang-1p13
            field $v : reader : param //= _can_run qw[v];

            #~ https://ziglang.org/documentation/0.13.0/#Exporting-a-C-Library
            #~ zig build-lib mathtest.zig -dynamic
            field $zig : reader : param //= _can_run qw[zig];
            #
            ADJUST {
            }

            sub _filemap( $file, $language //= () ) {
                #
                ($_) = $file =~ m[\.(?=[^.]*\z)([^.]+)\z]i;
                $language //=                                                     #
                    /^(?:ada|adb|ads|ali)$/i                  ? 'Ada' :           #
                    /^(?:asm|s|a)$/i                          ? 'Assembly' :      #
                    /^(?:c(?:c|pp|xx))$/i                     ? 'CPP' :           #
                    /^c$/i                                    ? 'C' :             #
                    /^c3$/i                                   ? 'C3' :            #
                    /^d$/i                                    ? 'D' :             #
                    /^cobol$/i                                ? 'Cobol' :         #
                    /^csharp$/i                               ? 'CSharp' :        #
                    /^crystal$/i                              ? 'Crystal' :       #
                    /^futhark$/i                              ? 'Futhark' :       #
                    /^go$/i                                   ? 'Go' :            #
                    /^haskell$/i                              ? 'Haskell' :       #
                    /^nim$/i                                  ? 'Nim' :           #
                    /^odin$/i                                 ? 'Odin' :          #
                    /^ace$/i                                  ? 'Eiffel' :        #
                    /^(?:f(?:or)?|f(?:77|90|95|0[38]|18)?)$/i ? 'Fortran' :       #
                    /^m+$/i                                   ? 'ObjectiveC' :    #
                    /^p$/i                                    ? 'P' :             #
                    /^v$/i                                    ? 'VLang' :         #
                    ();
                ( 'Affix::Compiler::File::' . ${language} )->new( path => $file );
            }
            #
            method compile() {
                @objs = grep {defined} map { $_->compile($flags) } @$source;
            }

            method link() {
                return () unless grep { $_->exists } @objs;
                system( $linker, $flags->{ldflags} // (), '-shared', '-o', $libname->stringify, ( map { $_->absolute->stringify } @objs ) ) ? () :
                    $libname;
            }

            #~ field $cxx;
            #~ field $d;
            #~ field $crystal;
        };

        class Affix::Compiler::File {
            use Config     qw[%Config];
            use Path::Tiny qw[];
            field $path  : reader : param;
            field $flags : reader : param //= ();
            field $obj   : reader : param //= ();
            ADJUST {
                $path = Path::Tiny::path($path) unless builtin::blessed $path;
                $obj //= $path->sibling( $path->basename(qr/\..+?$/) . $Config{_o} );
            }
            method compile() {...}
        }

        class Affix::Compiler::File::CPP : isa(Affix::Compiler::File) {

            # https://learn.microsoft.com/en-us/cpp
            # https://gcc.gnu.org/
            # https://clang.llvm.org/
            #~ https://www.intel.com/content/www/us/en/developer/tools/oneapi/dpc-compiler.html
            #~ https://www.ibm.com/products/c-and-c-plus-plus-compiler-family
            #~ https://docs.oracle.com/cd/E37069_01/html/E37073/gkobs.html
            #~ https://www.edg.com/c
            #~ https://www.circle-lang.org/site/index.html
            field $compiler : reader : param //= Affix::Compiler::_can_run qw[g++]

                #~ clang++ cl icpx ibm-clang++ CC eccp circle]
                ;

            method compile($flags) {
                system( $compiler, '-g', '-c', '-fPIC', $flags->{cxxflags} // (), $self->path, '-o', $self->obj ) ? () : $self->obj;
            }
        }

        class Affix::Compiler::File::C : isa(Affix::Compiler::File) {
            use Config qw[%Config];
            field $compiler : reader : param //= Affix::Compiler::_can_run $Config{cc}, qw[gcc]

                #~ clang cl icx ibm-clang CC eccp circle]
                ;

            method compile($flags) {
                system( $compiler, '-g', '-c', '-Wall', '-fPIC', $flags->{cflags} // (), $self->path, '-o', $self->obj ) ? () : $self->obj;
            }
        }

        class Affix::Compiler::File::Fortran : isa(Affix::Compiler::File) {

            # GNU, Intel, Intel Classic
            my $compiler = Affix::Compiler::_can_run qw[gfortran ifx ifort];

            method compile($flags) {
                my $obj = $self->obj;
                my $src = $self->path;
                warn qq`gfortran -shared -o $obj $src`;
                `gfortran -shared -o $obj $src`;
                $obj;

             #~ $self->obj
             #~ unless system grep {defined} $compiler, '-shared', ( Affix::Platform::Windows() ? () : '-fPIC' ), $flags->{fflags} // (), $self->path,
             #~ '-o', $self->obj;
            }
        }

        class Affix::Compiler::File::D : isa(Affix::Compiler::File) {
            use Config qw[%Config];
            field $compiler : reader : param //= Affix::Compiler::_can_run qw[dmd];

            method compile($flags) {
                system( $compiler, '-c', ( Affix::Platform::Windows() ? () : '-fPIC' ), $flags->{dflags} // (), $self->path, '-of=' . $self->obj ) ?
                    () : $self->obj;
            }
        }

        class Affix::Compiler::FortranXXXXXX : isa(Affix::Compiler) {
            use Config     qw[%Config];
            use IPC::Cmd   qw[can_run];
            use Path::Tiny qw[path];
            field $exe      : reader;
            field $compiler : reader;
            field $linker   : reader;
            #
            ADJUST {
                if ( $exe = can_run('gfortran') ) {
                    $compiler = method( $file, $obj, $flags ) {
                        system $self->exe, qw[-c -fPIC], $file;
                        die "failed to execute: $!\n"                                                                           if $? == -1;
                        die sprintf "child died with signal %d, %s coredump\n", ( $? & 127 ), ( $? & 128 ) ? 'with' : 'without' if $? & 127;
                        $obj
                    };
                    $linker = method($objs) {
                        system $self->exe, qw[-shared], ( map { $_->stringify } @$objs ), '-o blah.so';
                        die "failed to execute: $!\n"                                                                           if $? == -1;
                        die sprintf "child died with signal %d, %s coredump\n", ( $? & 127 ), ( $? & 128 ) ? 'with' : 'without' if $? & 127;
                        'ok!'
                    };
                }
                elsif ( $exe = can_run('ifx') )   { }
                elsif ( $exe = can_run('ifort') ) { }
            }
            #
            method compile( $file, $obj //= (), $flags //= '' ) {
                $file = path($file)->absolute unless builtin::blessed $file;
                $obj //= $file->sibling( $file->basename(qr/\..+?$/) . $Config{_o} );
                try {
                    return $compiler->( $self, $file, $obj, $flags );
                }
                catch ($err) { warn $err; }
            }

            method link($objs) {
                $objs = [ map { builtin::blessed $_ ? $_ : path($_)->absolute } @$objs ];
                return () unless @$objs;
                try {
                    return $linker->( $self, $objs );
                }
                catch ($err) { warn $err; }
            }
        }

        class Affix::Compiler::File::Dxxx {
            use Config     qw[%Config];
            use IPC::Cmd   qw[can_run];
            use Path::Tiny qw[];
            field $exe      : reader;
            field $compiler : reader;
            field $linker   : reader;
            field $path     : reader : param;
            #
            ADJUST {
                if ( $exe = can_run('dmd') ) {
                    $compiler = method( $file, $obj, $flags ) {
                        system $self->exe, qw[-c -fPIC], $file->stringify;
                        die "failed to execute: $!\n"                                                                           if $? == -1;
                        die sprintf "child died with signal %d, %s coredump\n", ( $? & 127 ), ( $? & 128 ) ? 'with' : 'without' if $? & 127;
                        $obj
                    };
                    $linker = method($objs) {
                        system $self->exe, qw[-shared], ( map { $_->stringify } @$objs ), '-o blah.so';
                        die "failed to execute: $!\n"                                                                           if $? == -1;
                        die sprintf "child died with signal %d, %s coredump\n", ( $? & 127 ), ( $? & 128 ) ? 'with' : 'without' if $? & 127;
                        'ok!'
                    };
                }
            }
            #
            method compile( $file, $obj //= (), $flags //= '' ) {
                $file = Path::Tiny::path($file)->absolute unless builtin::blessed $file;
                $obj //= $file->sibling( $file->basename(qr/\..+?$/) . $Config{_o} );
                try {
                    return $compiler->( $self, $file->stringify, $obj, $flags );
                }
                catch ($err) { warn $err; }
            }

            method link($objs) {
                $objs = [ map { builtin::blessed $_ ? $_ : Path::Tiny::path($_)->absolute } @$objs ];
                return () unless @$objs;
                try {
                    return $linker->( $self, $objs );
                }
                catch ($err) { warn $err; }
            }
        }
    }
}
1;
__END__
Copyright (C) Sanko Robinson.

This library is free software; you can redistribute it and/or modify it under
the terms found in the Artistic License 2. Other copyrights, terms, and
conditions may apply to data transmitted through this module.
