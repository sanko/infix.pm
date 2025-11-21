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
            Int8 UInt8 Int16 UInt16 Int32 UInt32 Int64 UInt64 Int128 UInt128
            Float32 Float64
            Size_t SSize_t
            String WString
            Pointer Array Struct Union Enum Callback CodeRef Complex Vector
            Packed VarArgs
            M256 M256d M512 M512d M512i]
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

    # Primitives
    sub Void ()       {'void'}
    sub Bool ()       {'bool'}
    sub Char ()       {'char'}
    sub UChar ()      {'uchar'}
    sub SChar ()      {'schar'}
    sub WChar ()      {'ushort'}       # Standard mapping for wchar_t on most modern systems
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

    # Fixed Width Types
    sub Int8 ()    {'sint8'}
    sub UInt8 ()   {'uint8'}
    sub Int16 ()   {'sint16'}
    sub UInt16 ()  {'uint16'}
    sub Int32 ()   {'sint32'}
    sub UInt32 ()  {'uint32'}
    sub Int64 ()   {'sint64'}
    sub UInt64 ()  {'uint64'}
    sub Int128 ()  {'sint128'}
    sub UInt128 () {'uint128'}
    sub Float32 () {'float32'}
    sub Float64 () {'float64'}

    # System Types
    sub Size_t ()  {'size_t'}
    sub SSize_t () {'ssize_t'}

    # SIMD
    sub M256 ()  {'m256'}
    sub M256d () {'m256d'}
    sub M512 ()  {'m512'}
    sub M512d () {'m512d'}
    sub M512i () {'m512i'}

    # Semantic aliases and convienient types
    sub String ()  {'*char'}
    sub WString () {'*ushort'}
    sub SV()       {'SV'}

    # Constructors
    # Pointer[ Int ] -> *int
    sub Pointer : prototype($) {
        my $type = ref( $_[0] ) ? $_[0]->[0] : $_[0];
        return '*' . ( $type // 'void' );
    }

    # Array[ Int, 10 ] -> [10:int]
    sub Array : prototype($) {
        my ( $type, $size ) = @{ $_[0] };
        return "[$size:$type]";
    }

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

    # Struct[ id => Int, score => Double ] -> {id:int,score:double}
    sub Struct : prototype($) { _build_aggregate( $_[0], '{%s}' ) }

    # Union[ i => Int, f => Float ] -> <i:int,f:float>
    sub Union : prototype($) { _build_aggregate( $_[0], '<%s>' ) }

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

    # Enum[ Int ] -> e:int
    # Enum[ [ K=>V, ... ], Int ] -> e:int (We ignore the values for the signature)
    sub Enum : prototype($) {
        my $args = $_[0];
        my $type = ( ref($args) eq 'ARRAY' && @$args > 1 ) ? $args->[1] : 'int';
        return "e:$type";
    }

    # Special marker for Variadic functions
    sub VarArgs () {';'}

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

    # Demo lib builder
    class Affix::Compiler {
        field $name         : param : reader;
        field $source       : param : reader;
        field $version      : param : reader //= undef;
        field $cleanup      : param : reader //= 1;       # Default to cleaning up temp files
        field $debug        : param : reader //= 0;
        field $include_dirs : param : reader //= [];
        #
        field $build_dir : reader;
        field $lib_file  : reader;
        field @objects   : reader;
        field $has_cpp   : reader = 0;                    # Track if we need C++ linking

        #
        ADJUST {
            $build_dir = tempdir( CLEANUP => $cleanup );

            # Determine library extension (.dll, .so, .dylib) based on Config
            my $ext = $Config::Config{so};
            $ext = 'dll' if $^O eq 'MSWin32';

            # Naming convention: Windows usually doesn't prefix 'lib', Unix does.
            my $prefix   = ( $^O eq 'MSWin32' || $name =~ /^lib/ ) ? '' : 'lib';
            my $filename = sprintf( "%s%s.%s", $prefix, $name, $ext );

            # Support versioned libs if requested (e.g. libfoo.so.1.2)
            $filename .= ".$version" if defined $version && $^O ne 'MSWin32';
            $lib_file = $build_dir->child($filename)->absolute;

            # Map source strings/paths to File handler objects
            $source = [ map { $self->_map_file($_) } @$source ];
        }

        method _map_file($file) {
            $file = File::Spec->rel2abs($file);    # Ensure absolute path

            # Get extension
            my ( $name, $path, $suffix ) = fileparse( $file, qr/\.[^.]+$/ );

            # fileparse puts the extension in $suffix (including the dot)
            my $ext = $suffix;
            $ext =~ s/^\.//;

            # Detect language by extension
            my $type = 'C';
            if ( $ext =~ /^(cpp|cxx|cc|c\+\+)$/i ) {
                $type    = 'CPP';
                $has_cpp = 1;
            }
            elsif ( $ext =~ /^(f90|f95|f03|for|f)$/i ) {
                $type = 'Fortran';
            }
            elsif ( $ext =~ /^d$/i ) {
                $type = 'D';
            }
            elsif ( $ext =~ /^rs$/i ) {
                $type = 'Rust';
            }

            # Add other mappings here...
            my $class = "Affix::Compiler::File::$type";
            return $class->new( path => $file, build_dir => $build_dir, debug => $debug, include_dirs => $include_dirs );
        }

        method compile() {
            @objects = ();
            for my $src (@$source) {
                push @objects, $src->compile();
            }
            return $self;
        }

        method link() {
            return unless @objects;
            require IPC::Cmd;

            # If we have any Rust files, they usually compile directly to DLLs,
            # so we might skip linking or handle it differently.
            # For standard C/C++/Fortran, we proceed here.
            # Choose linker: Use C++ compiler if we have C++ objects, else C compiler
            my $linker = $has_cpp ? ( IPC::Cmd::can_run('c++') || IPC::Cmd::can_run('g++') || IPC::Cmd::can_run('clang++') ) : $Config::Config{cc};

            # Common Link Flags
            my @cmd = ($linker);

            # Shared library flags
            if ( $^O eq 'MSWin32' ) {
                push @cmd, '-shared';

                # MinGW needs specific flags sometimes, MSVC needs /LD
                # Assuming MinGW/GCC environment based on previous context
            }
            else {
                push @cmd, '-shared', '-fPIC';

                # macOS specific install_name if needed
                push @cmd, '-Wl,-install_name,' . basename($lib_file) if $^O eq 'darwin';
            }

            # Output file
            push @cmd, '-o', $lib_file;

            # Objects
            push @cmd, @objects;

            # Libraries (math, etc)
            push @cmd, $Config::Config{libs};
            warn "[LINK] @cmd\n" if $debug;
            if ( system(@cmd) != 0 ) {
                die "Linking failed: $?";
            }
            return $lib_file;
        }
    }

    # -----------------------------------------------------------------------------
    # Base File Class
    # -----------------------------------------------------------------------------
    class Affix::Compiler::File {
        use Config qw[%Config];
        field $path         : param : reader;
        field $build_dir    : param : reader;
        field $debug        : param : reader //= 0;
        field $include_dirs : param : reader //= [];
        field $obj_file     : reader;
        ADJUST {
            # Extract filename without extension to build object path
            my ( $name, $dirs, $suffix ) = fileparse( $path, qr/\.[^.]+$/ );

            # $build_dir / $filename . .o
            $obj_file = File::Spec->catfile( $build_dir, $name . $Config::Config{_o} );
        }
        method compile() { die "Abstract method compile() called" }

        method _run(@cmd) {
            warn "[COMPILE] @cmd\n" if $debug;
            if ( system(@cmd) != 0 ) {
                die "Compilation failed for " . basename($path) . ": $?";
            }
            return $obj_file;
        }

        method _includes() {
            return map {"-I$_"} @$include_dirs;
        }
    }

    # -----------------------------------------------------------------------------
    # Language Implementations
    # -----------------------------------------------------------------------------
    class Affix::Compiler::File::C : isa(Affix::Compiler::File) {
        use Config qw[%Config];

        method compile() {

            # Use Perl's configured C compiler
            my $cc    = $Config::Config{cc};
            my @flags = ( '-c', '-fPIC', $self->_includes );

            # Add debug flags if requested
            push @flags, '-g' if $self->debug;
            $self->_run( $cc, @flags, $self->path, '-o', $self->obj_file );
        }
    }

    class Affix::Compiler::File::CPP : isa(Affix::Compiler::File) {

        method compile() {
            require IPC::Cmd;

            # Try to find a C++ compiler
            my $cxx   = IPC::Cmd::can_run('c++') || IPC::Cmd::can_run('g++') || IPC::Cmd::can_run('clang++') or die "No C++ compiler found in PATH";
            my @flags = ( '-c', '-fPIC', '-std=c++17', $self->_includes );    # Default to modern C++
            push @flags, '-g' if $self->debug;
            $self->_run( $cxx, @flags, $self->path, '-o', $self->obj_file );
        }
    }

    class Affix::Compiler::File::Fortran : isa(Affix::Compiler::File) {

        method compile() {
            require IPC::Cmd;
            my $fc    = IPC::Cmd::can_run('gfortran') || IPC::Cmd::can_run('g77') or die "No Fortran compiler found";
            my @flags = ( '-c', '-fPIC' );
            $self->_run( $fc, @flags, $self->path, '-o', $self->obj_file );
        }
    }

    class Affix::Compiler::File::D : isa(Affix::Compiler::File) {

        method compile() {
            require IPC::Cmd;
            my $dc = IPC::Cmd::can_run('dmd') || IPC::Cmd::can_run('gdc') || IPC::Cmd::can_run('ldc2') or die "No D compiler found";

            # DMD flags are slightly different
            my @flags = ( '-c', '-fPIC' );
            if ( $dc =~ /dmd/ ) {
                $self->_run( $dc, @flags, $self->path, '-of=' . $self->obj_file );
            }
            else {
                $self->_run( $dc, @flags, $self->path, '-o', $self->obj_file );
            }
        }
    }

    class Affix::Compiler::File::Rust : isa(Affix::Compiler::File) {

        # Rust is special; it usually compiles directly to a library or object
        # Here we compile to a .o to be linked by the main linker
        method compile() {
            require IPC::Cmd;
            my $rustc = IPC::Cmd::can_run('rustc') or die "No Rust compiler found";

            # --emit=obj creates a standard object file we can link with gcc/ld
            $self->_run( $rustc, '--emit=obj', $self->path, '-o', $self->obj_file );
        }
    }
    1;
};
1;
__END__
Copyright (C) Sanko Robinson.

This library is free software; you can redistribute it and/or modify it under
the terms found in the Artistic License 2. Other copyrights, terms, and
conditions may apply to data transmitted through this module.
