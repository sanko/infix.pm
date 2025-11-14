package Affix 1.00 {    # 'FFI' is my middle name!

    #~ G|-----------------------------------|-----------------------------------||
    #~ D|--------------------------4---5~---|--4--------------------------------||
    #~ A|--7~\-----4---44-/777--------------|------7/4~-------------------------||
    #~ E|-----------------------------------|-----------------------------------||
    use v5.40;
    use Carp                  qw[];
    use vars                  qw[@EXPORT_OK @EXPORT %EXPORT_TAGS];
    use warnings::register    qw[Type];
    use Config                qw[%Config];
    use File::Spec::Functions qw[rel2abs canonpath curdir path catdir];
    use File::Basename        qw[basename dirname];
    use File::Find            qw[find];
    #
    use Affix::Type          qw[:all];
    use Affix::Type::CodeRef qw[:all];
    use Affix::Type::Enum    qw[:all];
    use Affix::Type::Struct  qw[:all];
    use Affix::Type::Union   qw[:all];
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
        @Affix::Type::EXPORT_OK,         @Affix::Type::CodeRef::EXPORT_OK, @Affix::Type::Enum::EXPORT_OK,
        @Affix::Type::Struct::EXPORT_OK, @Affix::Type::Union::EXPORT_OK
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
};
1;
__END__
Copyright (C) Sanko Robinson.

This library is free software; you can redistribute it and/or modify it under
the terms found in the Artistic License 2. Other copyrights, terms, and
conditions may apply to data transmitted through this module.
