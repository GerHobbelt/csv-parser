#include <charconv>
#include <algorithm>
#include "csv.hpp"
#ifndef NDEBUG
#define NDEBUG
#endif

long double get_max(std::string file, std::string column, bool use_std = false);

long double get_max(std::string file, std::string column, bool use_std) {
	using namespace csv;
	long double max = -std::numeric_limits<long double>::infinity();
	CSVReader reader(file);

	for (auto& row : reader) {
		auto field = row[column];
		long double out = 0;

		if (use_std) {
			auto _field = field.get<std::string_view>();
			auto data = _field.data();
			std::from_chars(
				data, data + _field.size(),
				out
			);
		}
		else {
			out = field.get<long double>();
		}

		if (out > max) {
			max = out;
		}
	}

	return max;
}

int main(int argc, char** argv) {
	using namespace csv;

	std::string file("./bbb.xrslog");


	// Get information 
	//auto info = get_file_info(file);
	//std::cout << file << std::endl
	//    << "Columns: " << internals::format_row(info.col_names, ", ")
	//    << "Dimensions: " << info.n_rows << " rows x " << info.n_cols << " columns" << std::endl
	//    << "Delimiter: " << info.delim << std::endl;


	// Set format
	CSVFormat format;
	// line below dows not work. As C++ a UTF-8 character literal value cannot occupy more than one code unit
	// auto c = u8'§';
	char32_t cc = U'§'; // = 167 can check in python ord(§) = 167, but it is encoded as 0xC2A7

	unsigned int my_delimiter = 0xa7c2;
	format.delimiter(U'§') //§, alternatively, we can use format.delimiter({ '\t', ',', ... }) to tell the CSV guesser which delimiters to try out
		.quote('"') // must set if it conatains quto escaping i.e. to use "" represetnts " in side a string
		.header_row(0);   // Header is on 0th row (zero-indexed)
		// .no_header();  // Parse CSVs without a header row

	// Read the csv file 
	// FF: better to rename it to CSVFile
	CSVReader reader(file, format);

	// Display the basic info 
	std::cout << "=====================Basic Info=========================" << std::endl;
	std::cout << "Reading data from: " << file << std::endl;
	std::cout << "Format delim: " << format.get_delim() << std::endl;
	std::cout << "Format header pos:" << format.get_header() << std::endl;
	std::cout << "Format quoting enabled: " << format.is_quoting_enabled() << std::endl;
	std::cout << "Format trim chars:"; for (auto c : format.get_trim_chars()) std::cout << c << std::endl;
	std::cout << std::endl << std::endl;

	
	// Tip: After the initialization of the reader, we can 
	// 1. Get the columns if it has columns. or empty vector if not column names found in the csv file.
	std::cout << "===================== Cols =========================" << std::endl;
	std::vector<std::string> column_names = reader.get_col_names();
	for (auto& name : column_names) std::cout << name << " ";
	std::cout << std::endl << std::endl;


	// Tip: this lib do not provide reader[i] to randomly access the rows it is lazy in reading csv file.
	// we can use the iterator to get all rows one by one.
	// NOTE: the first row is not enen if the file has header(has been trimed out).
	std::cout << "===================== Rows =========================" << std::endl;
	for (CSVRow& row : reader) {
		
		//Tip: Once we got a row we can get the fields from the row. 
		// 1. Get fields with random access row[i] or with the column name 
		CSVField f0 = row[0];
		CSVField msg = row["message"];

		// 2. Get fields count
		size_t fields_count = row.size();
		// 3. Now e can access each filels raw string value.
		// NOTE: field.get() return raw string in the CSV file. 
		// TODO: How to get the string without escaping the quoto e.g. "This is ""THE"" book" ==> This is "THE" book ?
		for (auto i = 0; i < row.size(); i++) {
			std::cout << "-------- " << column_names[i] << std::endl;
			// row[i] is CSVField type
			std::cout << row[i].get() << std::endl;
		}

		// 4. or iterate all the field in a row like this
		for (auto& field : row) 
		{
			// code with field
		}
		std::cout << std::endl << std::endl;


		// 5. we can  convert string in the file to other type
		// 1. judge type of the field and try to conver the filed to a number
		// NOTE: this judge is time consuming if we check the type every time for a field.
		// better to judge once and catch the type to use it all the time.
		std::cout << "===================== Convert =========================" << std::endl;
		CSVField sid = row["sequenceid"];
		if (sid.is_str()) {
			std::cout << "This field is string: " << sid.get() << std::endl;
		}
		else if (sid.is_float()) {
			std::cout << "This field is float: " << sid.get<double>() << std::endl;
		}
		else if (sid.is_int()) {
			std::cout << "This field is int: " << sid.get<long>() << std::endl;
		}
	
		
		// NOTE: the we can also get the columns with the row
		auto all_cols = row.get_col_names();

	}
	return 0;
}