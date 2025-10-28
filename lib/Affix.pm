package Affix 1.00 {    # 'FFI' is my middle name!

    #~ G|-----------------------------------|-----------------------------------||
    #~ D|--------------------------4---5~---|--4--------------------------------||
    #~ A|--7~\-----4---44-/777--------------|------7/4~-------------------------||
    #~ E|-----------------------------------|-----------------------------------||
    use v5.40;
    use Carp               qw[];
    use vars               qw[@EXPORT_OK @EXPORT %EXPORT_TAGS];
    use warnings::register qw[Type];
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
};
1;
