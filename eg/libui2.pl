use v5.36;
use FindBin '$Bin';
use lib '../lib', 'lib';
use blib;
use lib $Bin;
use Carp qw[croak];
use libui;
$|++;
#
my $mem = Affix::malloc(1024);
warn $mem;
warn $$mem;
use Data::Dump;
ddx $mem;

#~ exit;
# Callbacks
# This sub is called when the user clicks the close button on the window.
# We need to tell libui to quit the main event loop.
# It must return a true value to allow the window to close.
sub on_closing ( $window, $data ) {
    use Data::Dump;
    ddx [ $window, $data ];
    say "Window closing. Bye!";
    LibUI::uiQuit();
    return 1;
}

# This sub is called when the button is clicked.
# The third argument is the user data we pass when setting up the callback.
# Here, we're passing a reference to the label widget.
sub on_button_clicked ( $button, $label ) {
    state $count = 0;
    $count++;
    LibUI::uiLabelSetText( $label, "You clicked the button $count time(s)!" );
}

# -- Main Program --
# Initialize the library. An empty hash is fine for default options.
my $init_error = LibUI::uiInit( \{ Size => 1024 } );
die "Error initializing libui: $init_error" if $init_error;

# Create the main window
my $main_win = LibUI::uiNewWindow( "Hello World!", 400, 200, 1 );
LibUI::uiWindowSetMargined( $main_win, 1 );    # Add padding around the edges

# Set up the closing callback
LibUI::uiWindowOnClosing( $main_win, \&on_closing, 'Bye' );
LibUI::uiWindowOnContentSizeChanged(
    $main_win,
    sub ( $window, $etc ) {
        my ( $w, $h );
        LibUI::uiWindowContentSize( $main_win, \$w, \$h );
        warn sprintf 'w: %d, h: %d', int $w, int $h;
    },
    'Bye'
);

# Create a vertical box to arrange widgets
my $vbox = LibUI::uiNewVerticalBox();
LibUI::uiBoxSetPadded( $vbox, 1 );
LibUI::uiWindowSetChild( $main_win, $vbox );    # Add the box to the window

# Create a label
my $label = LibUI::uiNewLabel("Welcome to libui-ng from Perl!");
LibUI::uiBoxAppend( $vbox, $label, 0 );         # Add label to box, not stretchy

# Create a button
my $button = LibUI::uiNewButton("Click Me");
LibUI::uiBoxAppend( $vbox, $button, 0 );        # Add button to box, not stretchy

# Set up the button's click callback.
# We pass a reference to the $label as the user data, so we can access it
# inside the callback.
LibUI::uiButtonOnClicked( $button, \&on_button_clicked, $label );

# Show the window and start the main event loop
LibUI::uiControlShow($main_win);
#
LibUI::uiTimer(
    10,
    sub ($blah) {

        #print '.';
        use Data::Dump;
        ddx $blah;
        return 10;
    },
    \%ENV
);
#
use Data::Dump;
ddx $main_win;
warn $$main_win;

#~ exit;
LibUI::uiMain();

# Clean up (this is only reached after uiQuit() is called)
LibUI::uiUninit();
say "Program finished cleanly.";
