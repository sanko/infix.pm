# example2_drawing_fixed.pl
use v5.38;
use FindBin '$Bin';
use lib '../lib', 'lib';
use blib;
use lib $Bin;
use LibUI qw[:all];
use Affix qw[:all];    # Import the new, explicit cast function

# Shared data for our histogram
my @data_points = ( 10, 80, 55, 30, 95, 25, 60 );
my $selected_point_color;    # This will be a brush struct

# This is the main drawing callback for the uiArea widget.
# It receives a pointer to the handler, the area handle, and a hashref of parameters.
sub handler_draw ( $handler, $area, $params ) {

    # Create a brush for the bar color (a pleasant blue)
    my $brush = { Type => LibUI::uiDrawBrushTypeSolid, R => 0.1, G => 0.4, B => 0.8, A => 1.0, };
    my $path  = LibUI::uiDrawNewPath(LibUI::uiDrawFillModeWinding);
    LibUI::uiDrawPathAddRectangle( $path, 0, 0, $params->{AreaWidth}, $params->{AreaHeight} );
    LibUI::uiDrawPathEnd($path);

    # Fill the background white
    my $white_brush = { Type => LibUI::uiDrawBrushTypeSolid, R => 1, G => 1, B => 1, A => 1 };
    LibUI::uiDrawFill( $params->{Context}, $path, $white_brush );
    LibUI::uiDrawFreePath($path);
    my $bar_width = $params->{AreaWidth} / scalar @data_points;
    for my $i ( 0 .. $#data_points ) {
        my $bar_height = ( $data_points[$i] / 100 ) * $params->{AreaHeight};
        my $x_pos      = $i * $bar_width;
        my $y_pos      = $params->{AreaHeight} - $bar_height;
        $path = LibUI::uiDrawNewPath(LibUI::uiDrawFillModeWinding);
        LibUI::uiDrawPathAddRectangle( $path, $x_pos, $y_pos, $bar_width, $bar_height );
        LibUI::uiDrawPathEnd($path);

        # Use a different color for the last bar, which is controlled by the slider
        my $current_brush = ( $i == $#data_points ) ? $selected_point_color : $brush;
        LibUI::uiDrawFill( $params->{Context}, $path, $current_brush );
        LibUI::uiDrawFreePath($path);
    }
}

# Main Program
die "Failed to init LibUI" if uiInit( \{ Size => 0 } );

# Define the color for the bar that the slider will control
$selected_point_color = { Type => LibUI::uiDrawBrushTypeSolid, R => 0.8, G => 0.2, B => 0.1, A => 1.0, };

# 1. EXPLICITLY CAST THE PERL HASH TO A MANAGED C STRUCT OBJECT
# The user is now responsible for ensuring $handler lives long enough.
# In this script, it lives for the entire duration of the program.
my $handler = { Draw => \&handler_draw, MouseEvent => sub { }, MouseCrossed => sub { }, DragBroken => sub { }, KeyEvent => sub { return 0 }, },;
my $win     = LibUI::uiNewWindow( "Drawing Area", 400, 300, 0 );
LibUI::uiWindowOnClosing( $win, sub { uiQuit(); 1 }, undef );
LibUI::uiWindowSetMargined( $win, 1 );
my $vbox = LibUI::uiNewVerticalBox();
LibUI::uiBoxSetPadded( $vbox, 1 );
LibUI::uiWindowSetChild( $win, $vbox );

# 2. PASS THE MANAGED POINTER OBJECT TO THE C FUNCTION
# The Affix marshaller will now detect this is a special object
# and pass its underlying C pointer.
my $drawing_area = LibUI::uiNewArea($handler);
LibUI::uiBoxAppend( $vbox, $drawing_area, 1 );    # Make it stretchy

# Add a slider to control the last data point
my $slider = LibUI::uiNewSlider( 0, 100 );
LibUI::uiSliderSetValue( $slider, $data_points[-1] );
LibUI::uiBoxAppend( $vbox, $slider, 0 );          # Not stretchy

# When the slider changes, update the data and tell the area to redraw
LibUI::uiSliderOnChanged(
    $slider,
    sub {
        $data_points[-1] = LibUI::uiSliderValue($slider);
        LibUI::uiAreaQueueRedrawAll($drawing_area);
    },
    undef
);
LibUI::uiControlShow($win);
LibUI::uiMain();
LibUI::uiUninit();

# When $handler goes out of scope, its DESTROY method (Affix_free_pin)
# will be called, automatically freeing the C struct memory.
