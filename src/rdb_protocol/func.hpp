#ifndef RDB_PROTOCOL_FUNC_HPP_
#define RDB_PROTOCOL_FUNC_HPP_

#include <map>
#include <vector>

#include "utils.hpp"

#include "containers/ptr_bag.hpp"
#include "containers/scoped.hpp"
#include "protob/protob.hpp"
#include "rdb_protocol/js.hpp"
#include "rdb_protocol/term.hpp"
#include "rpc/serialize_macros.hpp"

namespace ql {

class func_t : public ptr_baggable_t, public pb_rcheckable_t {
public:
    func_t(env_t *env, js::id_t id, term_t *parent);
    func_t(env_t *env, const Term2 *_source);
    // Some queries, like filter, can take a shortcut object instead of a
    // function as their argument.
    static func_t *new_filter_func(env_t *env, const datum_t *obj,
                                   const pb_rcheckable_t *root);
    static func_t *new_identity_func(env_t *env, const datum_t *obj,
                                     const pb_rcheckable_t *root);
    val_t *call(const std::vector<const datum_t *> &args);
    // Prefer these two version of call.
    val_t *call(const datum_t *arg);
    val_t *call(const datum_t *arg1, const datum_t *arg2);
    bool filter_call(env_t *env, const datum_t *arg);

    void dump_scope(std::map<int, Datum> *out) const;
    bool is_deterministic() const;

private:
    // Pointers to this function's arguments.
    std::vector<const datum_t *> argptrs;
    term_t *body; // body to evaluate with functions bound

    // This is what's serialized over the wire.
    friend class wire_func_t;
    const Term2 *source;
    bool implicit_bound;

    // TODO: make this smarter (it's sort of slow and shitty as-is)
    std::map<int, const datum_t **> scope;

    term_t *js_parent;
    env_t *js_env;
    js::id_t js_id;
};

class js_result_visitor_t : public boost::static_visitor<val_t *> {
public:
    typedef val_t *result_type;

    js_result_visitor_t(env_t *_env, term_t *_parent) : env(_env), parent(_parent) { }

    // This JS evaluation resulted in an error
    result_type operator()(const std::string err_val) const {
        rfail_target(parent, "%s", err_val.c_str());
        unreachable();
    }

    // This JS call resulted in a JSON value
    result_type operator()(const boost::shared_ptr<scoped_cJSON_t> json_val) const {
        return parent->new_val(new datum_t(json_val, env));
    }

    // This JS evaluation resulted in an id for a js function
    result_type operator()(const id_t id_val) const {
        return parent->new_val(new func_t(env, id_val, parent));
    }

private:
    env_t *env;
    term_t *parent;
};

RDB_MAKE_PROTOB_SERIALIZABLE(Term2);
RDB_MAKE_PROTOB_SERIALIZABLE(Datum);
// Used to serialize a function (or gmr) over the wire.
class wire_func_t : public pb_rcheckable_t {
public:
    wire_func_t();
    virtual ~wire_func_t() { }
    wire_func_t(env_t *env, func_t *_func);
    wire_func_t(const Term2 &_source, std::map<int, Datum> *_scope);
    func_t *compile(env_t *env);
protected:
    // We cache a separate function for every environment.
    std::map<env_t *, func_t *> cached_funcs;

    Term2 source;
    std::map<int, Datum> scope;
public:
    //RDB_MAKE_ME_SERIALIZABLE_2(source, scope);
};

// This is a hack because MAP, FILTER, REDUCE, and CONCATMAP are all the same.
namespace derived {
// For whatever reason RDB_MAKE_ME_SERIALIZABLE_3 wasn't happy inside of the
// template, so, you know, yeah.
class serializable_wire_func_t : public wire_func_t {
public:
    serializable_wire_func_t() : wire_func_t() { }
    serializable_wire_func_t(env_t *env, func_t *func) : wire_func_t(env, func) { }
    serializable_wire_func_t(const Term2 &_source, std::map<int, Datum> *_scope)
        : wire_func_t(_source, _scope) { }
    RDB_MAKE_ME_SERIALIZABLE_2(source, scope);
};
enum simple_funcs { MAP, FILTER, REDUCE, CONCATMAP };
template<int fconst>
class derived_wire_func_t : public serializable_wire_func_t {
public:
    derived_wire_func_t() : serializable_wire_func_t() { }
    derived_wire_func_t(env_t *env, func_t *func)
        : serializable_wire_func_t(env, func) { }
    derived_wire_func_t(const Term2 &_source, std::map<int, Datum> *_scope)
        : serializable_wire_func_t(_source, _scope) { }
};
} // namespace derived
typedef derived::derived_wire_func_t<derived::MAP> map_wire_func_t;
typedef derived::derived_wire_func_t<derived::FILTER> filter_wire_func_t;
typedef derived::derived_wire_func_t<derived::REDUCE> reduce_wire_func_t;
typedef derived::derived_wire_func_t<derived::CONCATMAP> concatmap_wire_func_t;

// Count is a fake function because we don't need to send anything.
struct count_wire_func_t { RDB_MAKE_ME_SERIALIZABLE_0() };

// Grouped Map Reduce
class gmr_wire_func_t {
public:
    gmr_wire_func_t() { }
    gmr_wire_func_t(env_t *env, func_t *_group, func_t *_map, func_t *_reduce)
        : group(env, _group), map(env, _map), reduce(env, _reduce) { }
    func_t *compile_group(env_t *env) { return group.compile(env); }
    func_t *compile_map(env_t *env) { return map.compile(env); }
    func_t *compile_reduce(env_t *env) { return reduce.compile(env); }
private:
    map_wire_func_t group;
    map_wire_func_t map;
    reduce_wire_func_t reduce;
public:
    RDB_MAKE_ME_SERIALIZABLE_3(group, map, reduce);
};

// Evaluating this returns a `func_t` wrapped in a `val_t`.
class func_term_t : public term_t {
public:
    func_term_t(env_t *env, const Term2 *term);
private:
    virtual bool is_deterministic_impl() const;
    virtual val_t *eval_impl();
    virtual const char *name() const { return "func"; }
    func_t *func;
};

} // namespace ql
#endif // RDB_PROTOCOL_FUNC_HPP_
