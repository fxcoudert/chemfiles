// Chemfiles, a modern library for chemistry file reading and writing
// Copyright (C) 2015-2016 Guillaume Fraux and contributors
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/
#include "chemfiles/Error.hpp"
#include "chemfiles/files/NcFile.hpp"
using namespace chemfiles;

void chemfiles::nc::check(int status, const std::string& message) {
    if (status != NC_NOERR) {
        throw FileError(message + "\n    (NetCDF error is '" +
                        nc_strerror(status) + "')");
    }
}

size_t chemfiles::nc::hyperslab_size(const count_t& count) {
    size_t counted = 1;
    for (auto value : count) {
        counted *= value;
    }
    return counted;
}

nc::NcVariable::NcVariable(NcFile& file, netcdf_id_t var): file_(file), var_id_(var) {}

std::vector<size_t> nc::NcVariable::dimmensions() const {
    int size = 0;
    int status = nc_inq_varndims(file_.netcdf_id(), var_id_, &size);
    nc::check(status, "Could not get the number of dimmensions");

    auto dim_ids = std::vector<netcdf_id_t>(static_cast<size_t>(size), 0);
    status = nc_inq_vardimid(file_.netcdf_id(), var_id_, dim_ids.data());
    nc::check(status, "Could not get the dimmensions id");

    std::vector<size_t> result;
    for (auto dim_id: dim_ids) {
        size_t length = 0;
        status = nc_inq_dimlen(file_.netcdf_id(), dim_id, &length);
        check(status, "Could not get the dimmensions size");
        result.push_back(length);
    }
    return result;
}


std::string nc::NcVariable::attribute(const std::string& name) const {
    size_t size = 0;
    int status = nc_inq_attlen(file_.netcdf_id(), var_id_, name.c_str(), &size);
    nc::check(status, "Can not read attribute id '" + name + "'");

    std::string value(size, ' ');
    status = nc_get_att_text(file_.netcdf_id(), var_id_, name.c_str(), &value[0]);
    nc::check(status, "Can not read attribute text '" + name + "'");
    return value;
}


void nc::NcVariable::add_attribute(const std::string& name, const std::string& value) {
    assert(file_.nc_mode() == NcFile::DEFINE && "File must be in define mode to add attribute");
    int status = nc_put_att_text(file_.netcdf_id(), var_id_, name.c_str(), value.size(), value.c_str());
    nc::check(status, "Can not set attribute'" + name + "'");
}

std::vector<float> nc::NcFloat::get(count_t start, count_t count) const {
    auto size = hyperslab_size(count);
    auto result = std::vector<float>(size, 0.0);
    int status = nc_get_vara_float(
        file_.netcdf_id(), var_id_,
        start.data(), count.data(),
        result.data()
    );
    nc::check(status, "Could not read variable");
    return result;
}

void nc::NcFloat::add(count_t start, count_t count, std::vector<float> data) {
    assert(data.size() == hyperslab_size(count));
    int status = nc_put_vara_float(
        file_.netcdf_id(), var_id_,
        start.data(), count.data(),
        data.data()
    );
    nc::check(status, "Could not put data in variable");
}

void nc::NcChar::add(std::string data) {
    int status = nc_put_var_text(file_.netcdf_id(), var_id_, data.c_str());
    nc::check(status, "Could not put text data in variable");
}

void nc::NcChar::add(std::vector<std::string> data) {
    size_t i = 0;
    for (auto string: data) {
        string.resize(STRING_MAXLEN, '\0');
        size_t start[] = {i, 0};
        size_t count[] = {1, STRING_MAXLEN};
        int status = nc_put_vara_text(
            file_.netcdf_id(), var_id_,
            start, count,
            string.c_str()
        );
        nc::check(status, "Could not put vector text data in variable");
        i++;
    }
}

/******************************************************************************/

NcFile::NcFile(const std::string& filename, File::Mode mode)
    : File(filename, mode), file_id_(-1), nc_mode_(DATA) {
    auto status = NC_NOERR;

    switch (mode) {
    case File::READ:
        status = nc_open(filename.c_str(), NC_NOWRITE, &file_id_);
        break;
    case File::APPEND:
        status = nc_open(filename.c_str(), NC_WRITE, &file_id_);
        break;
    case File::WRITE:
        status = nc_create(filename.c_str(), NC_64BIT_OFFSET | NC_CLASSIC_MODEL, &file_id_);
        // Put the file in DATA mode. This can only fail for bad id, which we
        // check later.
        nc_enddef(file_id_);
        break;
    default:
        throw FileError(std::string("Got a bad file mode: ") + static_cast<char>(mode));
    }

    nc::check(status, "Could not open the file '" + filename + "'");
}

NcFile::~NcFile() noexcept {
    auto status = nc_close(file_id_);
    assert(status == NC_NOERR);
}

void NcFile::set_nc_mode(NcMode mode) {
    if (mode == nc_mode_) {
        return;
    }

    if (mode == DATA) {
        auto status = nc_enddef(file_id_);
        nc::check(status, "Could not change to data mode");
        nc_mode_ = DATA;
    } else if (mode == DEFINE) {
        auto status = nc_redef(file_id_);
        nc::check(status, "Could not change to define mode");
        nc_mode_ = DEFINE;
    }
}

NcFile::NcMode NcFile::nc_mode() const {
    return nc_mode_;
}

std::string NcFile::global_attribute(const std::string& name) const {
    size_t size = 0;
    auto status = nc_inq_attlen(file_id_, NC_GLOBAL, name.c_str(), &size);
    nc::check(status, "Can not read attribute '" + name + "'");

    std::string value(size, ' ');
    // &value[0] get a pointer to the first char in the string. In C++11, the
    // string
    // storage must be contiguous, so we can use it here. value.c_str() returns
    // a const
    // char *, and thus can not be used by nc_get_att_text.
    status = nc_get_att_text(file_id_, NC_GLOBAL, name.c_str(), &value[0]);
    nc::check(status, "Can not read attribute '" + name + "'");

    return value;
}

void NcFile::add_global_attribute(const std::string& name, const std::string& value) {
    assert(nc_mode() == DEFINE &&
           "File must be in define mode to add attribute");
    auto status = nc_put_att_text(file_id_, NC_GLOBAL, name.c_str(),
                                 value.size(), value.c_str());

    nc::check(status,
        "Could not add the \"" + name +
        "\" global attribute with value \"" + value
    );
}

size_t NcFile::dimension(const std::string& name) const {
    auto size = optional_dimension(name, static_cast<size_t>(-1));
    if (size == static_cast<size_t>(-1)) {
        throw FileError("Missing dimmension '" + name + "'");
    }
    return size;
}

size_t NcFile::optional_dimension(const std::string& name, size_t value) const {
    // Get the dimmension id
    auto dim_id = nc::netcdf_id_t(-1);
    auto status = nc_inq_dimid(file_id_, name.c_str(), &dim_id);
    if (dim_id == nc::netcdf_id_t(-1)) {
        return value;
    }
    nc::check(status, "Can not read dimmension '" + name + "'");

    // Get dimmension size
    size_t size = 0;
    status = nc_inq_dimlen(file_id_, dim_id, &size);
    nc::check(status, "Can not read dimmension '" + name + "'");

    return size;
}

void NcFile::add_dimension(const std::string& name, size_t value) {
    assert(nc_mode() == DEFINE &&
           "File must be in define mode to add dimmension");
    auto dim_id = nc::netcdf_id_t(-1);
    auto status = nc_def_dim(file_id_, name.c_str(), value, &dim_id);
    nc::check(status, "Can not add dimension \"" + name + "\"");
}

bool NcFile::variable_exists(const std::string& name) const {
    auto id = nc::netcdf_id_t(-1);
    auto status = nc_inq_varid(file_id_, name.c_str(), &id);
    return status == NC_NOERR;
}
