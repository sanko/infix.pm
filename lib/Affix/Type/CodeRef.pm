package Affix::Type::CodeRef 0.5 {
    use strict;
    use warnings;
    use Carp qw[];
    $Carp::Internal{ (__PACKAGE__) }++;
    use Scalar::Util qw[dualvar];
    use parent -norequire, 'Exporter', 'Affix::Type::Parameterized';
    our ( @EXPORT_OK, %EXPORT_TAGS );
    $EXPORT_TAGS{all} = [ @EXPORT_OK = qw[CodeRef] ];
    #
    # sub typedef : prototype($$) {
    #     my ( $self, $name ) = @_;
    #     no strict 'refs';
    #     warn 'TODO: generate mutators';
    #     #~ for my $key ( keys %{ $self->[5] } ) {
    #     #~ my $val = $self->[5]{$key};
    #     #~ *{ $name . '::' . $key } = sub () { dualvar $val, $key; };
    #     #~ }
    #     1;
    # }
    sub CodeRef : prototype($) {
        my (@elements) = @{ +shift };
        my ( $args, $ret ) = @elements;
        my $s = Affix::Type::CodeRef->new(
            sprintf( 'CodeRef[ [ %s ] => %s ]', join( ', ', @$args ), $ret ),    # SLOT_CODEREF_STRINGIFY
            Affix::CODEREF_FLAG(),                                               # SLOT_CODEREF_NUMERIC
            Affix::Platform::SIZEOF_INTPTR_T(),                                  # SLOT_CODEREF_SIZEOF
            Affix::Platform::ALIGNOF_INTPTR_T(),                                 # SLOT_CODEREF_ALIGNMENT
            0,                                                                   # SLOT_CODEREF_OFFSET
            undef, $args, $ret
        );

        # TODO:
        # $s->[ Affix::SLOT_CODEREF_ARGS() ] = $args;
        # $s->[ Affix::SLOT_CODEREF_SIG() ]  = join( '', map { chr $_ } @$args );
        $s;
    }
};
1;
