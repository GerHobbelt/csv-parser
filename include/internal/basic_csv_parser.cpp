#include "basic_csv_parser.hpp"

namespace csv {
    namespace internals {
        CSV_INLINE size_t get_file_size(csv::string_view filename) {
            std::ifstream infile(std::string(filename), std::ios::binary);
            const auto start = infile.tellg();
            infile.seekg(0, std::ios::end);
            const auto end = infile.tellg();

            return end - start;
        }

        CSV_INLINE std::string get_csv_head(csv::string_view filename) {
            return get_csv_head(filename, get_file_size(filename));
        }

        CSV_INLINE std::string get_csv_head(csv::string_view filename, size_t file_size) {
            const size_t bytes = 500000;

            std::error_code error;
            size_t length = std::min((size_t)file_size, bytes);
            auto mmap = mio::make_mmap_source(std::string(filename), 0, length, error);

            if (error) {
                throw std::runtime_error("Cannot open file " + std::string(filename));
            }

            return std::string(mmap.begin(), mmap.end());
        }

#ifdef _MSC_VER
#pragma region IBasicCVParser
#endif
        CSV_INLINE IBasicCSVParser::IBasicCSVParser(
            const CSVFormat& format,
            const ColNamesPtr& col_names
        ) : _col_names(col_names) {
            if (format.no_quote) {
                _parse_flags = internals::make_parse_flags(format.get_delim());
            }
            else {
                _parse_flags = internals::make_parse_flags(format.get_delim(), format.quote_char);
            }

            _ws_flags = internals::make_ws_flags(format.trim_chars);
        }

        CSV_INLINE void IBasicCSVParser::end_feed() {
            using internals::ParseFlags;

            bool empty_last_field = this->data_ptr
                && this->data_ptr->_data
                && !this->data_ptr->data.empty()
                && parse_flag(this->data_ptr->data.back()) == ParseFlags::DELIMITER;

            // Push field
            if (this->field_length > 0 || empty_last_field) {
                this->push_field();
            }

            // Push row
            if (this->current_row.size() > 0)
                this->push_row();
        }

        CSV_INLINE void IBasicCSVParser::parse_field() noexcept {
            using internals::ParseFlags;
            auto& in = this->data_ptr->data;



            // Trim off leading whitespace
            while (data_pos < in.size())
            {
                unsigned int c = 0;
                unsigned int len = next_glyph(&c, data_pos);
                if (ws_flag(c))
                    data_pos += len;
                else
                    break;
            }

            if (field_start == UNINITIALIZED_FIELD)
                field_start = (int)(data_pos - current_row_start());

            // Optimization: Since NOT_SPECIAL characters tend to occur in contiguous
            // sequences, use the loop below to avoid having to go through the outer
            // switch statement as much as possible
            while (data_pos < in.size())
            {
                unsigned int c = 0;
                unsigned int len = next_glyph(&c, data_pos);
                if (compound_parse_flag(c) == ParseFlags::NOT_SPECIAL)
                    data_pos += len;
                else
                    break;
            }

            field_length = data_pos - (field_start + current_row_start());

            // Here we assume white space just ocupy one byte.
            // TODO how to trim trailing whitespace if the white space is not single byte?
            // Trim off trailing whitespace, this->field_length constraint matters
            // when field is entirely whitespace
            for (size_t j = data_pos - 1; ws_flag(in[j]) && this->field_length > 0; j--)
                this->field_length--;
        }

        CSV_INLINE void IBasicCSVParser::push_field()
        {
            // Update
            if (field_has_double_quote) {
                fields->emplace_back(
                    field_start == UNINITIALIZED_FIELD ? 0 : (unsigned int)field_start,
                    field_length,
                    true
                );
                field_has_double_quote = false;

            }
            else {
                fields->emplace_back(
                    field_start == UNINITIALIZED_FIELD ? 0 : (unsigned int)field_start,
                    field_length
                );
            }

            current_row.row_length++;

            // Reset field state
            field_start = UNINITIALIZED_FIELD;
            field_length = 0;
        }


        // Convert UTF-8 to 32-bit character, process single character input.
        // A nearly-branchless UTF-8 decoder, based on work of Christopher Wellons (https://github.com/skeeto/branchless-utf8).
        // We handle UTF-8 decoding error by skipping forward.
        int IBasicCSVParser::TextCharFromUtf8(unsigned int* out_char, const char* in_text, const char* in_text_end)
        {
            static const char lengths[32] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 3, 3, 4, 0 };
            static const int masks[] = { 0x00, 0x7f, 0x1f, 0x0f, 0x07 };
            static const uint32_t mins[] = { 0x400000, 0, 0x80, 0x800, 0x10000 };
            static const int shiftc[] = { 0, 18, 12, 6, 0 };
            static const int shifte[] = { 0, 6, 4, 2, 0 };
            int len = lengths[*(const unsigned char*)in_text >> 3];
            int wanted = len + !len;

            if (in_text_end == NULL)
                in_text_end = in_text + wanted; // Max length, nulls will be taken into account.

            // Copy at most 'len' bytes, stop copying at 0 or past in_text_end. Branch predictor does a good job here,
            // so it is fast even with excessive branching.
            unsigned char s[4];
            s[0] = in_text + 0 < in_text_end ? in_text[0] : 0;
            s[1] = in_text + 1 < in_text_end ? in_text[1] : 0;
            s[2] = in_text + 2 < in_text_end ? in_text[2] : 0;
            s[3] = in_text + 3 < in_text_end ? in_text[3] : 0;

            // Assume a four-byte character and load four bytes. Unused bits are shifted out.
            *out_char = (uint32_t)(s[0] & masks[len]) << 18;
            *out_char |= (uint32_t)(s[1] & 0x3f) << 12;
            *out_char |= (uint32_t)(s[2] & 0x3f) << 6;
            *out_char |= (uint32_t)(s[3] & 0x3f) << 0;
            *out_char >>= shiftc[len];

            // Accumulate the various error conditions.
            int e = 0;
            e = (*out_char < mins[len]) << 6; // non-canonical encoding
            e |= ((*out_char >> 11) == 0x1b) << 7;  // surrogate half?
            e |= (*out_char > IM_UNICODE_CODEPOINT_MAX) << 8;  // out of range?
            e |= (s[1] & 0xc0) >> 2;
            e |= (s[2] & 0xc0) >> 4;
            e |= (s[3]) >> 6;
            e ^= 0x2a; // top two bits of each tail byte correct?
            e >>= shifte[len];

            if (e)
            {
                // No bytes are consumed when *in_text == 0 || in_text == in_text_end.
                // One byte is consumed in case of invalid first byte of in_text.
                // All available bytes (at most `len` bytes) are consumed on incomplete/invalid second to last bytes.
                // Invalid or incomplete input may consume less bytes than wanted, therefore every byte has to be inspected in s.
                wanted = std::min(wanted, !!s[0] + !!s[1] + !!s[2] + !!s[3]);
                *out_char = IM_UNICODE_CODEPOINT_INVALID;
            }

            return wanted;
        }


        CSV_INLINE int IBasicCSVParser::next_glyph(unsigned int* out_char, size_t pos)
        {
            auto& in = this->data_ptr->data;
            unsigned int c = (unsigned int)in[pos];
            unsigned int len = 1;
            if (c < 0x80)
            {
                // single byte char
                len = 1;
                *out_char = c;
            }
            else
            {
                // multi byte char
                len = TextCharFromUtf8(&c, in.data() + pos, in.data() + in.size());
                if (c == 0) // Malformed UTF-8?
                {
                    //TODOD 
                    std::exception("Malformed UTF-8");
                    std::exit(1);
                }
                *out_char = c;
            }
            return len;
        }

        /** @return The number of characters parsed that belong to complete rows */
        CSV_INLINE size_t IBasicCSVParser::parse()
        {
            using internals::ParseFlags;

            this->quote_escape = false;
            this->data_pos = 0;
            this->current_row_start() = 0;
            this->trim_utf8_bom();

            auto& in = this->data_ptr->data;
            while (this->data_pos < in.size()) {

                unsigned int c = 0;
                unsigned int len = next_glyph(&c, this->data_pos);
               
                switch (compound_parse_flag(c)) {
                case ParseFlags::DELIMITER:
                    this->push_field();
                    this->data_pos+=len;
                    break;

                case ParseFlags::NEWLINE:
                    this->data_pos+=len;

                    // Catches CRLF (or LFLF)
                    len = next_glyph(&c, this->data_pos);
                    if (this->data_pos < in.size() && parse_flag(c) == ParseFlags::NEWLINE)
                        this->data_pos+=len;

                    // End of record -> Write record
                    this->push_field();
                    this->push_row();

                    // Reset
                    this->current_row = CSVRow(data_ptr, this->data_pos, fields->size());
                    break;

                case ParseFlags::NOT_SPECIAL:
                    this->parse_field();
                    break;

                case ParseFlags::QUOTE_ESCAPE_QUOTE:
                    if (data_pos + 1 == in.size()) return this->current_row_start();
                    else{
                        unsigned int next_c = 0;
                        int next_len = next_glyph(&next_c, data_pos+len);
                        if (data_pos + next_len > in.size())
                        {
                            break;
                        }

                        ParseFlags flag = parse_flag(next_c);
                        if (flag >= ParseFlags::DELIMITER) {
                            quote_escape = false;
                            data_pos+=len;
                            break;
                        }
                        else if (flag == ParseFlags::QUOTE) {
                            // Case: Escaped quote
                            data_pos = data_pos + len + next_len;
                            this->field_length = this->field_length + len + next_len;
                            this->field_has_double_quote = true;
                            break;
                        }
                    }
                    
                    // Case: Unescaped single quote => not strictly valid but we'll keep it
                    this->field_length += len;
                    data_pos+=len;

                    break;

                default: // Quote (currently not quote escaped)
                    if (this->field_length == 0) {
                        quote_escape = true;
                        data_pos+=len;
                        
                        if (field_start == UNINITIALIZED_FIELD && data_pos < in.size())
                        {
                            // peek next char
                            int next_len = next_glyph(&c, this->data_pos);
                            if( !ws_flag(c))
                                field_start = (int)(data_pos - current_row_start());
                        }
                        break;
                    }

                    // Case: Unescaped quote
                    this->field_length+=len;
                    data_pos+=len;
                    break;
                }
            }

            return this->current_row_start();
        }

        CSV_INLINE void IBasicCSVParser::push_row() {
            current_row.row_length = fields->size() - current_row.fields_start;
            this->_records->push_back(std::move(current_row));
        }

        CSV_INLINE void IBasicCSVParser::reset_data_ptr() {
            this->data_ptr = std::make_shared<RawCSVData>();
            this->data_ptr->parse_flags = this->_parse_flags;
            this->data_ptr->col_names = this->_col_names;
            this->fields = &(this->data_ptr->fields);
        }

        CSV_INLINE void IBasicCSVParser::trim_utf8_bom() {
            auto& data = this->data_ptr->data;

            if (!this->unicode_bom_scan && data.size() >= 3) {
                if (data[0] == '\xEF' && data[1] == '\xBB' && data[2] == '\xBF') {
                    this->data_pos += 3; // Remove BOM from input string
                    this->_utf8_bom = true;
                }

                this->unicode_bom_scan = true;
            }
        }
#ifdef _MSC_VER
#pragma endregion
#endif

#ifdef _MSC_VER
#pragma region Specializations
#endif
        CSV_INLINE void MmapParser::next(size_t bytes = ITERATION_CHUNK_SIZE) {
            // Reset parser state
            this->field_start = UNINITIALIZED_FIELD;
            this->field_length = 0;
            this->reset_data_ptr();

            // Create memory map
            size_t length = std::min(this->source_size - this->mmap_pos, bytes);
            std::error_code error;
            this->data_ptr->_data = std::make_shared<mio::basic_mmap_source<char>>(mio::make_mmap_source(this->_filename, this->mmap_pos, length, error));
            this->mmap_pos += length;
            if (error) throw error;

            auto mmap_ptr = (mio::basic_mmap_source<char>*)(this->data_ptr->_data.get());

            // Create string view
            this->data_ptr->data = csv::string_view(mmap_ptr->data(), mmap_ptr->length());

            // Parse
            this->current_row = CSVRow(this->data_ptr);
            size_t remainder = this->parse();            

            if (this->mmap_pos == this->source_size || no_chunk()) {
                this->_eof = true;
                this->end_feed();
            }

            this->mmap_pos -= (length - remainder);
        }
#ifdef _MSC_VER
#pragma endregion
#endif
    }
}
