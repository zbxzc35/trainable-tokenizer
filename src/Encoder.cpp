#include <stdexcept>

#include "Encoder.hpp"
#include "configuration.hpp"

#ifdef USE_ICONV
#include <iconv.h>
#include <cerrno>
#else
#include <unicode/umachine.h>
#include <unicode/utypes.h>
#include <unicode/ucnv.h>
#endif

namespace trtok {

void Encoder::do_work() {
#ifdef USE_ICONV
	iconv_t cd = iconv_open(m_output_encoding.c_str(), "UTF-8");
	if (cd == iconv_t(-1))
		throw std::invalid_argument("The specified encoding is not supported.");
#else
	UErrorCode error_code = U_ZERO_ERROR;
	UConverter *in_converter_p = ucnv_open("UTF-8", &error_code);
	UConverter *out_converter_p = ucnv_open(m_output_encoding.c_str(), &error_code);
	if (U_FAILURE(error_code)) {
		throw std::invalid_argument("The specified encoding is not supported.");
	}
#endif

	const size_t in_buffer_size = ENCODER_BUFFER_SIZE;
	const size_t out_buffer_size = ENCODER_BUFFER_SIZE * 4;

	char in_buffer[in_buffer_size];
	char *in_buffer_end = in_buffer + in_buffer_size;
	char out_buffer[out_buffer_size];
	char *out_buffer_end = out_buffer + out_buffer_size;
	
	// Where to are new bytes read?
	char *in_read_p = in_buffer;
	// Where are the bytes to be converted?
#ifdef USE_ICONV
	char *in_conv_p = in_buffer;
#else
	char const *in_conv_p = in_buffer;
#endif
	// How many bytes are there to convert?
	size_t in_bytes_ready = 0;
	// Where to store the conversion output?
	char *out_p = out_buffer;
	// How much space is there for conversion output?
	size_t out_bytes_left = out_buffer_size;

#ifdef USE_ICU
	// ICU uses UTF-16 natively so conversion must go through UTF-16.
	// The UTF-16 intermediate string is stored in the pivot buffer.
	const size_t pivot_buffer_size = ENCODER_BUFFER_SIZE * 2;

	UChar pivot_buffer[pivot_buffer_size];
	UChar *pivot_in_p = pivot_buffer;
	UChar *pivot_out_p = pivot_buffer;
	UChar *pivot_buffer_end = pivot_buffer + pivot_buffer_size;
#endif

	// We convert while there is something to read or we have
	// leftovers in the input buffer.
	while (*m_input_stream_p || (in_read_p != in_buffer)) {
		if (*m_input_stream_p) {
			m_input_stream_p->read(in_read_p, in_buffer_end - in_read_p);
			in_bytes_ready += m_input_stream_p->gcount();
		}


		in_conv_p = in_buffer;
		out_p = out_buffer;
		out_bytes_left = out_buffer_size;
#ifdef USE_ICONV
		size_t converted = iconv(cd, &in_conv_p, &in_bytes_ready, &out_p, &out_bytes_left);
		if ((converted == -1) && (errno == EILSEQ)) {
			// TODO: Remove this check because we can be sure that
			// a) the UTF-8 sequences are generated by us and therefore
			//    are noise-free
			// b) the target encoding is the same as the source encoding
			//    (for the whole program) so we should never have to output
			//    a value impossible for the target encoding
			// Hence this occurs only when using the Encoder outside
			// the final program on foreign input.
			std::string bad_sequence;
			bad_sequence.push_back(*in_conv_p++);
			// We read all the continuation bytes (10xxxxxx) along with
			// the first byte of the invalid sequence.
			while ((in_conv_p != in_buffer_end) && (*in_conv_p >> 6 == 2))
				bad_sequence.push_back(*in_conv_p++);
			throw std::logic_error("Encountered character (" + bad_sequence + ") invalid for target encoding.");
		}
#else
		ucnv_convertEx(out_converter_p, in_converter_p,
			&out_p, out_buffer + out_bytes_left,
			&in_conv_p, in_buffer + in_bytes_ready,
			pivot_buffer, &pivot_in_p, &pivot_out_p, pivot_buffer_end,
			FALSE, m_input_stream_p->eof(), &error_code);
		in_bytes_ready = (in_buffer + in_bytes_ready) - in_conv_p;
		out_bytes_left = out_buffer_end - out_p;
#endif
		m_output_stream_p->write(out_buffer, out_p - out_buffer);

		// There might have been leftovers at the end of the input buffer
		// which weren't processed by the converter either due to not enough
		// space in the output buffer or the input was trailed by an incomplete
		// multibyte sequence. We "read" (copy) these leftovers from the rest
		// of the buffer to its beginnig.
		char const *in_copy_p = in_conv_p;
		in_read_p = in_buffer;
		while (in_copy_p != in_conv_p + in_bytes_ready) {
			*in_read_p++ = *in_copy_p++;
		}
	}

#ifdef USE_ICONV
	iconv_close(cd);
#else
	ucnv_close(in_converter_p);
	ucnv_close(out_converter_p);
#endif

	m_output_stream_p->flush();
}

}
