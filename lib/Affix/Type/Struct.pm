package Affix::Type::Struct 0.5 {
    use strict;
    use warnings;
    use Carp qw[];
    $Carp::Internal{ (__PACKAGE__) }++;
    use Scalar::Util qw[dualvar];
    use parent -norequire, 'Exporter', 'Affix::Type::Parameterized';
    our ( @EXPORT_OK, %EXPORT_TAGS );
    $EXPORT_TAGS{all} = [ @EXPORT_OK = qw[Struct] ];

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

    sub Struct : prototype($) {
        my (@types) = @{ +shift };
        warnings::warnif( 'Affix::Type', 'Odd number of elements in struct fields' ) if @types % 2;
        my @fields;
        my $sizeof = 0;
        my $packed = 0;
        my @store_;

        #for my ( $field, $subtype ) (@types) { # requires perl 5.36
        for ( my $i = 0; $i < $#types; $i += 2 ) {
            my $field   = $types[$i];
            my $subtype = $types[ $i + 1 ];

            #~ warn sprintf '%10s => %d', $field, $subtype->{offset};
            $subtype->{name} = $field;    # field name
            push @store_, bless { %{$subtype} }, ref $subtype;
            push @fields, sprintf '%s: %s', $field, $subtype;

            #~ warn sprintf 'After:  struct size: %d, element size: %d', $sizeof, $__sizeof;
        }
        my $s = Affix::Type::Struct->new( sprintf( '{ %s }', join( ', ', @fields ) ) );
        return $s;
    }
};
1;
