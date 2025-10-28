package Affix::Type::Enum 0.5 {
    use strict;
    use warnings;
    use Carp qw[];
    $Carp::Internal{ (__PACKAGE__) }++;
    use Scalar::Util qw[dualvar];
    use parent 'Exporter';
    our ( @EXPORT_OK, %EXPORT_TAGS );
    $EXPORT_TAGS{all} = [ @EXPORT_OK = qw[Enum IntEnum UIntEnum CharEnum] ];
    {
        @Affix::Type::Enum::ISA    = 'Affix::Type';
        @Affix::Type::IntEnum::ISA = @Affix::Type::UIntEnum::ISA = @Affix::Type::CharEnum::ISA = 'Affix::Type::Enum';
    }

    sub _Enum : prototype($) {
        my (@elements) = @{ +shift };
        my $fields;
        my $index = 0;
        my $enum;
        for my $element (@elements) {
            if ( ref $element eq 'ARRAY' ) {
                ( $element, $index ) = @$element if ref $element eq 'ARRAY';
                push @$fields, sprintf q[[%s => '%s']], $element, $index;
            }
            else {
                push @$fields, qq['$element'];
            }
            if ( $index =~ /[+|-|\*|\/|^|%|\D]/ ) {
                $index =~ s[(\w+)][$enum->{$1}//$1]xeg;
                $index = eval $index;
            }
            $enum->{$element} = $index++;
        }
        return $fields, $enum;
    }

    sub Enum : prototype($) {
        my ( $text, $enum ) = &_Enum;
        my $s = Affix::Type::Enum->new(
            sprintf( 'Enum[ %s ]', join ', ', @$text ),    # SLOT_CODEREF_STRINGIFY
            Affix::INT_FLAG(),                             # SLOT_CODEREF_NUMERIC
            Affix::Platform::SIZEOF_INT(),                 # SLOT_CODEREF_SIZEOF
            Affix::Platform::ALIGNOF_INT(),                # SLOT_CODEREF_ALIGNMENT
        );
        $s->{enum} = $enum;
        $s;
    }

    sub IntEnum : prototype($) {
        my ( $text, $enum ) = &_Enum;
        my $s = Affix::Type::IntEnum->new(
            sprintf( 'IntEnum[ %s ]', join ', ', @$text ),    # SLOT_CODEREF_STRINGIFY
            Affix::INT_FLAG(),                                # SLOT_CODEREF_NUMERIC
            Affix::Platform::SIZEOF_INT(),                    # SLOT_CODEREF_SIZEOF
            Affix::Platform::ALIGNOF_INT(),                   # SLOT_CODEREF_ALIGNMENT
        );
        $s->{enum} = $enum;
        $s;
    }

    sub UIntEnum : prototype($) {
        my ( $text, $enum ) = &_Enum;
        my $s = Affix::Type::UIntEnum->new(
            sprintf( 'UIntEnum[ %s ]', join ', ', @$text ),    # SLOT_CODEREF_STRINGIFY
            Affix::UINT_FLAG(),                                # SLOT_CODEREF_NUMERIC
            Affix::Platform::SIZEOF_UINT(),                    # SLOT_CODEREF_SIZEOF
            Affix::Platform::ALIGNOF_UINT(),                   # SLOT_CODEREF_ALIGNMENT
        );
        $s->{enum} = $enum;
        $s;
    }

    sub CharEnum : prototype($) {

        # TODO: user SUPER->new(...)
        my (@elements) = @{ +shift };
        my $text;
        my $index = 0;
        my $enum;
        for my $element (@elements) {
            ( $element, $index ) = @$element if ref $element eq 'ARRAY';
            if ( $index =~ /[+|-|\*|\/|^|%]/ ) {
                $index =~ s[(\w+)][$enum->{$1}//$1]xeg;
                $index =~ s[\b(\D)\b][ord $1]xeg;
                $index = eval $index;
            }
            push @$enum, [ $element, $index =~ /\D/ ? ord $index : $index ];
            push @$text, sprintf '[%s => %s]', $element, $index;
            $index++;
        }
        my $s = Affix::Type::CharEnum->new(
            sprintf( 'CharEnum[ %s ]', join ', ', @$text ),    # SLOT_CODEREF_STRINGIFY
            Affix::CHAR_FLAG(),                                # SLOT_CODEREF_NUMERIC
            Affix::Platform::SIZEOF_CHAR(),                    # SLOT_CODEREF_SIZEOF
            Affix::Platform::ALIGNOF_CHAR(),                   # SLOT_CODEREF_ALIGNMENT
        );
        $s->{enum} = $enum;
        $s;
    }

    sub typedef : prototype($$) {
        my ( $self, $name ) = @_;
        no strict 'refs';
        for my $key ( keys %{ $self->{enum} } ) {
            my $val = $self->{enum}{$key};
            *{ $name . '::' . $key } = sub () { dualvar $val, $key; };
        }
        1;
    }
};
1;
