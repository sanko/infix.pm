package Affix::Type::Union 0.5 {
    use strict;
    use warnings;
    use Carp qw[];
    $Carp::Internal{ (__PACKAGE__) }++;
    use Scalar::Util qw[dualvar];
    use parent -norequire, 'Exporter', 'Affix::Type::Parameterized';
    our ( @EXPORT_OK, %EXPORT_TAGS );
    $EXPORT_TAGS{all} = [ @EXPORT_OK = qw[Union] ];

    sub typedef : prototype($$) {
        my ( $self, $name ) = @_;
        no strict 'refs';
        warn 'TODO: generate mutators';

        #~ for my $key ( keys %{ $self->[5] } ) {
        #~ my $val = $self->[5]{$key};
        #~ *{ $name . '::' . $key } = sub () { dualvar $val, $key; };
        #~ }
        1;
    }

    sub Union : prototype($) {
        my (@types) = @{ +shift };
        my @fields;
        my $sizeof    = 0;
        my $packed    = 0;
        my $alignment = 0;
        my @store;
        for ( my $i = 0; $i < $#types; $i += 2 ) {
            my $field   = $types[$i];
            my $subtype = $types[ $i + 1 ];
            $subtype->{offset} = 0;
            $subtype->{name}   = $field;                     # field name
            push @store, bless {%$subtype}, ref $subtype;    # clone
            push @fields, sprintf '%s: %s', $field, $subtype;
        }
        __PACKAGE__->new( sprintf( '< %s >', join ', ', @fields ) );
    }
};
1;
