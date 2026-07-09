#include "inline_command.h"

namespace swarmmind {

InlineCommandParser::InlineCommandParser(std::string open_marker,
                                         std::string close_marker,
                                         CommandHandler handler)
    : open_(std::move(open_marker))
    , close_(std::move(close_marker))
    , handler_(std::move(handler))
{
}

std::string InlineCommandParser::feed(const std::string& delta) {
    std::string out;
    for (char c : delta) {
        step(c, out);
    }
    return out;
}

std::string InlineCommandParser::flush() {
    std::string out;
    switch (state_) {
    case State::TEXT:
        break;
    case State::OPEN_MATCH:
        out = match_;  // partial opening marker was just text
        break;
    case State::PAYLOAD:
    case State::CLOSE_MATCH:
        // Unterminated command — restore it verbatim as text
        out = open_ + payload_ + match_;
        break;
    }
    state_ = State::TEXT;
    match_.clear();
    payload_.clear();
    return out;
}

void InlineCommandParser::step(char c, std::string& out) {
    switch (state_) {
    case State::TEXT:
        if (c == open_[0]) {
            state_ = State::OPEN_MATCH;
            match_ = c;
        } else {
            out += c;
        }
        break;

    case State::OPEN_MATCH:
        if (c == open_[match_.size()]) {
            match_ += c;
            if (match_.size() == open_.size()) {
                state_ = State::PAYLOAD;
                match_.clear();
                payload_.clear();
            }
        } else {
            // Mismatch: first held char was plain text; reprocess the rest
            std::string rest = match_.substr(1);
            rest += c;
            out += match_[0];
            match_.clear();
            state_ = State::TEXT;
            for (char r : rest) {
                step(r, out);
            }
        }
        break;

    case State::PAYLOAD:
        if (c == close_[0]) {
            state_ = State::CLOSE_MATCH;
            match_ = c;
        } else {
            payload_ += c;
        }
        break;

    case State::CLOSE_MATCH:
        if (c == close_[match_.size()]) {
            match_ += c;
            if (match_.size() == close_.size()) {
                handler_(payload_);
                match_.clear();
                payload_.clear();
                state_ = State::TEXT;
            }
        } else {
            // Mismatch: held chars belong to the payload; reprocess the rest
            payload_ += match_[0];
            std::string rest = match_.substr(1);
            rest += c;
            match_.clear();
            state_ = State::PAYLOAD;
            for (char r : rest) {
                step(r, out);
            }
        }
        break;
    }
}

bool extract_loose_command(const std::string& payload,
                           std::string& name, nlohmann::json& args) {
    // name: "name" : "<identifier>" — tool names carry no inner quotes
    size_t np = payload.find("\"name\"");
    if (np == std::string::npos) return false;
    size_t nq = payload.find('"', payload.find(':', np + 6) + 1);
    if (nq == std::string::npos) return false;
    size_t nq_end = payload.find('"', nq + 1);
    if (nq_end == std::string::npos) return false;
    name = payload.substr(nq + 1, nq_end - nq - 1);

    // args: first "key" inside the args object
    size_t ap = payload.find("\"args\"", nq_end);
    if (ap == std::string::npos) return false;
    size_t kq = payload.find('"', payload.find('{', ap) + 1);
    if (kq == std::string::npos) return false;
    size_t kq_end = payload.find('"', kq + 1);
    if (kq_end == std::string::npos) return false;
    std::string key = payload.substr(kq + 1, kq_end - kq - 1);

    // value: opening quote after the key, closing at the payload's last quote
    size_t vq = payload.find('"', payload.find(':', kq_end + 1) + 1);
    if (vq == std::string::npos) return false;
    size_t vq_end = payload.rfind('"');
    if (vq_end <= vq) return false;

    args = nlohmann::json::object();
    args[key] = payload.substr(vq + 1, vq_end - vq - 1);
    return true;
}

} // namespace swarmmind
