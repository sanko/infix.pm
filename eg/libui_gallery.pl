use v5.36;
use FindBin '$Bin';
use lib '../lib', 'lib';
use blib;
use lib $Bin;
use Carp qw[croak];
use libui;
my ( $main_win, $slider, $spinbox, $progress_bar );

# Callback for when the slider or spinbox value changes
sub on_slider_spinbox_changed ( $control, $data ) {

    # Get the new value from the control that triggered the callback
    my $new_value = ( $control == $slider ) ? LibUI::uiSliderValue($slider) : LibUI::uiSpinboxValue($spinbox);

    # Update the other controls to stay in sync
    LibUI::uiSliderSetValue( $slider, $new_value );
    LibUI::uiSpinboxSetValue( $spinbox, $new_value );
    LibUI::uiProgressBarSetValue( $progress_bar, $new_value );
}

# Callback for the main window's close button
sub on_closing {
    LibUI::uiQuit();
    return 1;
}

# --- Main Program ---
# 1. Initialize the library
die "Failed to init LibUI" if LibUI::uiInit( { Size => 0 } );

# 2. Create the main window
$main_win = LibUI::uiNewWindow( "Controls Gallery", 500, 200, 0 );
LibUI::uiWindowSetMargined( $main_win, 1 );
LibUI::uiWindowOnClosing( $main_win, \&on_closing, undef );

# 3. Create a layout
# Main horizontal box to split the window into two panes
my $hbox = LibUI::uiNewHorizontalBox();
LibUI::uiBoxSetPadded( $hbox, 1 );
LibUI::uiWindowSetChild( $main_win, $hbox );

# Left pane for sliders and progress
my $vbox_left = LibUI::uiNewVerticalBox();
LibUI::uiBoxSetPadded( $vbox_left, 1 );
LibUI::uiBoxAppend( $hbox, $vbox_left, 1 );    # 1 = stretchy

# Right pane for other controls
my $vbox_right = LibUI::uiNewVerticalBox();
LibUI::uiBoxSetPadded( $vbox_right, 1 );
LibUI::uiBoxAppend( $hbox, $vbox_right, 1 );    # 1 = stretchy

# 4. Create and arrange widgets
# --- Left Pane ---
$spinbox      = LibUI::uiNewSpinbox( 0, 100 );
$slider       = LibUI::uiNewSlider( 0, 100 );
$progress_bar = LibUI::uiNewProgressBar();
LibUI::uiBoxAppend( $vbox_left, $spinbox,      0 );
LibUI::uiBoxAppend( $vbox_left, $slider,       0 );
LibUI::uiBoxAppend( $vbox_left, $progress_bar, 0 );

# --- Right Pane ---
my $group = LibUI::uiNewGroup("Options");
LibUI::uiGroupSetMargined( $group, 1 );
LibUI::uiBoxAppend( $vbox_right, $group, 1 );    # stretchy group
my $group_vbox = LibUI::uiNewVerticalBox();
LibUI::uiBoxSetPadded( $group_vbox, 1 );
LibUI::uiGroupSetChild( $group, $group_vbox );
my $checkbox = LibUI::uiNewCheckbox("Enable Something");
LibUI::uiCheckboxSetChecked( $checkbox, 1 );
LibUI::uiBoxAppend( $group_vbox, $checkbox, 0 );
my $separator = LibUI::uiNewHorizontalSeparator();
LibUI::uiBoxAppend( $group_vbox, $separator, 0 );
my $radio_buttons = LibUI::uiNewRadioButtons();
LibUI::uiRadioButtonsAppend( $radio_buttons, "Option 1" );
LibUI::uiRadioButtonsAppend( $radio_buttons, "Option 2" );
LibUI::uiRadioButtonsAppend( $radio_buttons, "Option 3" );
LibUI::uiBoxAppend( $group_vbox, $radio_buttons, 0 );

# 5. Register callbacks
LibUI::uiSliderOnChanged( $slider, \&on_slider_spinbox_changed, undef );
LibUI::uiSpinboxOnChanged( $spinbox, \&on_slider_spinbox_changed, undef );

# 6. Show the window and start the main event loop
LibUI::uiControlShow($main_win);
LibUI::uiMain();
LibUI::uiUninit();
say "Done.";
