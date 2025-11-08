# example3_form.pl
use FindBin '$Bin';
use lib '../lib', 'lib';
use blib;
use lib $Bin;
use LibUI qw[:all];
my ( $main_win, $name_entry, $color_button, $check );

# Callback for the "Submit" button
sub on_submit_clicked {

    # 1. Get text from the entry field
    # This returns an Affix::Pointer object, which might be a null pointer.
    my $name_ptr = LibUI::uiEntryText($name_entry);

    # 2. Dereference the pointer to get the string value for display
    # Use the correct ${...} syntax. The 'defined-or' operator handles the
    # case where the entry was empty, preventing "uninitialized value" warnings.
    my $name_str = ${$name_ptr} // '';

    # 3. Get the state of the checkbox
    my $is_checked = LibUI::uiCheckboxChecked($check) ? "Yes" : "No";

    # 4. Get the selected color
    my ( $r, $g, $b, $a );
    LibUI::uiColorButtonColor( $color_button, \$r, \$g, \$b, \$a );

    # 5. Format the final message
    my $msg = sprintf( "Hello, %s!\n\nSubscribed: %s\nFavorite Color (RGBA): %.2f, %.2f, %.2f, %.2f", $name_str, $is_checked, $r, $g, $b, $a );

    # 6. Show the results in a message box
    LibUI::uiMsgBox( $main_win, "Submission Received", $msg );

    # 7. Free the C string using the original pointer object
    LibUI::uiFreeText($name_ptr);
}
sub on_closing { LibUI::uiQuit(); 1 }

# --- Main Program ---
die "Failed to init LibUI" if LibUI::uiInit( { Size => 0 } );
$main_win = LibUI::uiNewWindow( "User Registration", 300, 150, 0 );
LibUI::uiWindowSetMargined( $main_win, 1 );
LibUI::uiWindowOnClosing( $main_win, \&on_closing, undef );
my $vbox = LibUI::uiNewVerticalBox();
LibUI::uiBoxSetPadded( $vbox, 1 );
LibUI::uiWindowSetChild( $main_win, $vbox );

# Use a uiForm for a nicely aligned label-and-field layout
my $form = LibUI::uiNewForm();
LibUI::uiFormSetPadded( $form, 1 );
LibUI::uiBoxAppend( $vbox, $form, 1 );             # stretchy
$name_entry = LibUI::uiNewEntry();
LibUI::uiFormAppend( $form, "Full Name", $name_entry, 0 );
$color_button = LibUI::uiNewColorButton();
LibUI::uiFormAppend( $form, "Favorite Color", $color_button, 0 );
$check = LibUI::uiNewCheckbox("Subscribe to newsletter");
LibUI::uiFormAppend( $form, "", $check, 0 );       # Empty label for alignment
my $submit_button = LibUI::uiNewButton("Submit");
LibUI::uiButtonOnClicked( $submit_button, \&on_submit_clicked, undef );
LibUI::uiBoxAppend( $vbox, $submit_button, 0 );    # not stretchy
LibUI::uiControlShow($main_win);
LibUI::uiMain();
LibUI::uiUninit();
