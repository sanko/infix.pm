#!/usr/bin/perl
use strict;
use warnings;
use File::Basename qw[dirname];
use FindBin        qw[$Bin];
use FindBin '$Bin';
use lib '../lib', 'lib';
use blib;
use lib $Bin;

# Ensure our custom LibUI module is in the path
use lib dirname($Bin);
use LibUI ':all';

# Our Application's Data Model
# This array of arrays is the "source of truth" for the table.
# Structure: [ Name (string), Age (string), Editable (0=no, 1=yes) ]
my @table_data = [
    [ "Alice",   "32", 0 ],    # Name is not editable
    [ "Bob",     "28", 1 ],    # Name and Age are editable
    [ "Charlie", "45", 1 ],
];

# Table Model Handler Callbacks
# These are the Perl subroutines that libui will call to get data for the table.
sub model_num_columns {3}                             # Name, Age, Editable Flag
sub model_column_type { uiTableValueTypeString() }    # All our data is string-based
sub model_num_rows    { scalar @table_data }

# Returns the value for a specific cell. This is the core data-providing function.
sub model_cell_value {
    my ( $handler, $model, $row, $col ) = @_;
    my $value = $table_data[$row][$col];
    return uiNewTableValueString( $value // "" );    # Must wrap the value in a uiTableValue
}

# Called by libui when a user edits a cell in the table.
sub model_set_cell_value {
    my ( $handler, $model, $row, $col, $value_obj ) = @_;

    # Extract the raw string from the uiTableValue object
    my $new_text = uiTableValueString($value_obj);
    print "User changed data[$row][$col] to '$new_text'\n";
    $table_data[$row][$col] = $new_text;
}

# Main Program
die "Failed to init LibUI" if LibUI::uiInit( { Size => 0 } );
my $window = uiNewWindow( "Affix Table View Demo", 500, 300, 0 );
uiWindowSetMargined( $window, 1 );
uiWindowOnClosing( $window, sub { uiQuit(); 1; }, undef );
my $vbox = uiNewVerticalBox();
uiBoxSetPadded( $vbox, 1 );
uiWindowSetChild( $window, $vbox );

# 1. Define the handler. This is just a Perl hash!
#    Affix automatically converts this to the C uiTableModelHandler struct.
my $model_handler = {
    NumColumns   => \&model_num_columns,
    ColumnType   => \&model_column_type,
    NumRows      => \&model_num_rows,
    CellValue    => \&model_cell_value,
    SetCellValue => \&model_set_cell_value,
};

# 2. Create the libui model object from our Perl handler.
my $model = uiNewTableModel($model_handler);

# 3. Create the table parameters, also as a simple Perl hash.
my $table_params = {
    Model                         => $model,
    RowBackgroundColorModelColumn => -1,       # Use default row colors
};

# 4. Create the table (view) itself.
my $table = uiNewTable($table_params);
uiBoxAppend( $vbox, $table, 1 );               # The '1' makes the table expand to fill space

# 5. Define the visual columns and map them to our data model columns.
#    uiTableAppendTextColumn(table, name, textDataColumn, editableFlagColumn)
uiTableAppendTextColumn( $table, "Full Name", 0, 2 );    # Text from col 0, edit flag from col 2
uiTableAppendTextColumn( $table, "Age",       1, 2 );    # Text from col 1, edit flag from col 2

# Controls to modify the model
my $hbox = uiNewHorizontalBox();
uiBoxSetPadded( $hbox, 1 );
uiBoxAppend( $vbox, $hbox, 0 );
my $add_button = uiNewButton("Add Row");
uiButtonOnClicked(
    $add_button,
    sub {
        # a. Modify our Perl data array
        push @table_data, [ "New Person", "0", 1 ];

        # b. Notify the model that a row was inserted so the view can update
        uiTableModelRowInserted( $model, $#table_data );
    },
    undef
);
uiBoxAppend( $hbox, $add_button, 0 );
my $del_button = uiNewButton("Delete Selected (Not Implemented)");
uiControlDisable($del_button);
uiBoxAppend( $hbox, $del_button, 0 );

# Show Window and Start Main Loop
uiControlShow($window);
uiMain();

# Cleanup
# It's good practice to free the model when the application closes.
uiFreeTableModel($model);
uiUninit();
