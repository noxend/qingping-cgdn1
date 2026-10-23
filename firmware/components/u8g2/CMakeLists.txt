# Vendored subset of u8g2 (new-BSD). Panel drivers and the full font set are
# dropped; a custom in-memory device and two fonts live beside this file.
# even_digits.py has rounded the dots and evened the gaps of logisoso42_tn.
idf_component_register(SRC_DIRS "." INCLUDE_DIRS ".")
