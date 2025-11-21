package Affix::Type 0.5 {
    use strict;
    use warnings;
    use Carp qw[];
    use warnings::register;
    use feature 'current_sub';    # perl 5.16+
    {
        package                   #hide
            Affix::Type::Parameterized 0.00 {
            use parent -norequire, 'Affix::Type';
            sub parameterized          {1}
            sub subtype : prototype($) { return shift->[ Affix::SLOT_SUBTYPE() ]; }
        }
    }
    $Carp::Internal{ (__PACKAGE__) }++;
    use parent 'Exporter';
    our ( @EXPORT_OK, %EXPORT_TAGS );
    $EXPORT_TAGS{all} = [
        @EXPORT_OK = qw[
        ]
    ];
    use overload '""' => sub {
        my $ret = $_[0]->{stringify};
        return $ret unless $_[0]->{const};
        return $ret if $_[0]->{depth};
        return 'Const[ ' . $ret . ' ]';
        },
        '0+' => sub { shift->{numeric} };
    sub parameterized($) {0}

    sub new : prototype($$;$$$$$$$) {
        my ( $pkg, $str, $flag, $sizeof, $align, $offset, $array_len, $subtypes, $cb_res ) = @_;
        die 'Please subclass Affix::Type' if $pkg eq __PACKAGE__;
        bless {
            stringify => $str,
            numeric   => $flag,
            sizeof    => $sizeof,
            alignment => $align,
            typedef   => undef,     # TODO
            const     => !1,
            depth     => 0,         # pointer depth

            # Optional
            offset => $offset // 0,           # TODO
            length => [ $array_len // () ],

            # Callbacks
            ( defined $subtypes ? ( subtypes => $subtypes ) : () ), ( defined $cb_res ? ( cb_res => $cb_res ) : () )
        }, $pkg;
    }

    # Types
    #~ sub Void()        { Affix::Type::Void->new('void'); }
    #~ sub Bool()        { Affix::Type::Bool->new('bool'); }
    #~ sub Char()        { Affix::Type::Char->new('char'); }
    #~ sub SChar()       { Affix::Type::SChar->new('sint8'); }
    #~ sub sint8         { Affix::Type::sint8->new('sint8'); }
    #~ sub UChar()       { Affix::Type::UChar->new('uchar'); }
    #~ sub WChar()       { Affix::Type::WChar->new('WChar'); }
    #~ sub Short()       { Affix::Type::Short->new('short'); }
    #~ sub UShort()      { Affix::Type::UShort->new('ushort'); }
    #~ sub Int ()        { Affix::Type::Int->new('int'); }
    #~ sub UInt ()       { Affix::Type::UInt->new('uint'); }
    #~ sub Long ()       { Affix::Type::Long->new('long'); }
    #~ sub ULong ()      { Affix::Type::ULong->new('ulong'); }
    #~ sub LongLong ()   { Affix::Type::LongLong->new('longlong'); }
    #~ sub ULongLong ()  { Affix::Type::ULongLong->new('ulonglong'); }
    #~ sub Float ()      { Affix::Type::Float->new('float'); }
    #~ sub Double ()     { Affix::Type::Double->new('double'); }
    #~ sub LongDouble () { Affix::Type::LongDouble->new('longdouble'); }
    #~ sub Size_t ()     { Affix::Type::Size_t->new('Size_t'); }
    #~ sub String()      { CORE::state $type //= Pointer( [ Const( [ Char() ] ) ] );  $type; }
    #~ sub WString()     { CORE::state $type //= Pointer( [ Const( [ WChar() ] ) ] ); $type; }
    #~ sub StdString ()  { Affix::Type::StdString->new('StdString'); }
    # TODO: CPPStruct
    #~ $pkg, $str, $flag, $sizeof, $align, $offset, $subtype, $array_len
    sub Pointer : prototype($) {
        my ( $subtype, $length ) = @{ +shift };
        $subtype->{depth}++;
        unshift @{ $subtype->{length} }, $length // -1;    # -1 forces fallback
        $subtype->{stringify} = '*' . $subtype->{stringify}

            #. ( defined $length ? ', ' . $length : '' )
            ;
        $subtype;
    }

    # Should only be used inside of a Pointer[]
    sub SV : prototype() { Affix::Type::SV->new( 'SV', Affix::SV_FLAG(), 0, 0 ); }

    #~ typedef ShortInt     => Short;
    #~ typedef SShort       => Short;
    #~ typedef SShortInt    => Short;
    #~ typedef UShortInt    => UShort;
    #~ typedef Signed       => Int;
    #~ typedef SInt         => Int;
    #~ typedef Unsigned     => UInt;
    #~ typedef LongInt      => Long;
    #~ typedef SLongInt     => Long;
    #~ typedef LongLongInt  => LongLong;
    #~ typedef SLongLong    => LongLong;
    #~ typedef SLongLongInt => LongLong;
    #~ typedef ULongLongInt => ULongLong;
    #~ typedef Str          => String;
    #~ typedef WStr         => WString;
    #~ typedef wchar_t => WChar;
    # Qualifier flags
    sub Const : prototype($) {
        my ( $subtype, @etc ) = @_ ? @{ +shift } : Void();    # Defaults to Pointer[Void]
        Carp::croak sprintf( 'Too may arguments in Pointer[ %s, %s ]', $subtype, join ', ', @etc ) if @etc;
        $subtype->{const}     = 1;
        $subtype->{stringify} = 'Const[ ' . $subtype->{stringify} . ' ]';
        $subtype;
    }
    @Affix::Type::Void::ISA = @Affix::Type::SV::ISA

        # Numerics
        = @Affix::Type::Bool::ISA  = @Affix::Type::Char::ISA     = @Affix::Type::SChar::ISA     = @Affix::Type::UChar::ISA = @Affix::Type::WChar::ISA
        = @Affix::Type::Short::ISA = @Affix::Type::UShort::ISA   = @Affix::Type::Int::ISA       = @Affix::Type::UInt::ISA = @Affix::Type::Long::ISA
        = @Affix::Type::ULong::ISA = @Affix::Type::LongLong::ISA = @Affix::Type::ULongLong::ISA = @Affix::Type::Float::ISA
        = @Affix::Type::Double::ISA
        = @Affix::Type::LongDouble::ISA
        #
        = @Affix::Type::sint8::ISA
        #
        = @Affix::Type::Size_t::ISA

        # Enumerations (subclasses handled in Affix::Type::Enum)
        = @Affix::Type::Enum::ISA

        # Pointers
        = @Affix::Type::String::ISA = @Affix::Type::WString::ISA = @Affix::Type::StdString::ISA

        # Typedef'd aliases
        = @Affix::Type::Str::ISA

        # Calling conventions
        = @Affix::CC::ISA
        #
        = @Affix::Type::Parameterized::ISA = 'Affix::Type';

    # Pointers
    @Affix::Type::Pointer::Unmanaged::ISA = 'Affix::Pointer';

    # Aggregates
    @Affix::Type::Union::ISA
        #
        = @Affix::Type::Pointer::ISA = @Affix::Type::CodeRef::ISA = @Affix::Type::Function::ISA = 'Affix::Type::Parameterized';
    sub This() { bless( [ 'This', THIS_FLAG(), undef, undef, undef ], 'Affix::CC::This' ); }
    #
    #~ sub intptr_t { Pointer [Void] }
}
1;
