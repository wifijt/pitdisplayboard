#ifndef SPONSORS_H
#define SPONSORS_H

#include <vector>
#include <string>

// Header is now static and set in code, but we can keep a reference here or just the list.
// Updating text as requested for reference, though logic will change to use static small font.
#define SPONSOR_HEADER_TEXT "Thank you to our sponsors"

const std::vector<std::string> SPONSOR_LIST = {
    "EBSCO Information Services",
    "New England Biolabs",
    "Analog Devices",
    "Ipswich Public Schools",
    "Institution for Savings",
    "Rotary Club of Ipswich",
    "Corning Foundation",
    "Applied Materials"
};

#endif // SPONSORS_H
