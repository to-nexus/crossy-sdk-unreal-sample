import os
import sys

import unreal


PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CSV_PATH = os.path.join(PROJECT_ROOT, "Localization", "DT_DappStrings.csv")
ASSET_PATH = "/Game/Localization/DT_DappStrings.DT_DappStrings"
ROW_STRUCT_PATH = "/Script/CrossySdkUnrealSamp.DappStringRow"


def fail(message):
    unreal.log_error(message)
    sys.exit(1)


if not os.path.isfile(CSV_PATH):
    fail("CSV not found: {}".format(CSV_PATH))

data_table = unreal.load_asset(ASSET_PATH)
if data_table is None:
    fail("DataTable asset not found: {}".format(ASSET_PATH))

row_struct = unreal.load_object(None, ROW_STRUCT_PATH)
if row_struct is None:
    fail("Row struct not found: {}".format(ROW_STRUCT_PATH))

ok = unreal.DataTableFunctionLibrary.fill_data_table_from_csv_file(
    data_table,
    CSV_PATH,
    row_struct,
)
if not ok:
    fail("Failed to import CSV into {}".format(ASSET_PATH))

package = data_table.get_outermost()
if not unreal.EditorLoadingAndSavingUtils.save_packages([package], only_dirty=False):
    fail("Failed to save package for {}".format(ASSET_PATH))

unreal.log("[done] Reimported {} into {}".format(CSV_PATH, ASSET_PATH))
