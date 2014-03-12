/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2013 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2013-2014 Jean-Francois Romang (Python module)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <Python.h>

#include <sstream>
#include <iostream>
#include <vector>
#include <algorithm>
#include <fstream>
#include <streambuf>
#include <stack>

#include "types.h"
#include "bitboard.h"
#include "evaluate.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "ucioption.h"
#include "notation.h"
#include "book.h"


using namespace std;

namespace
{
// FEN string of the initial position, normal chess
const char* StartFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// Keep track of position keys along the setup moves (from start position to the
// position just before to start searching). Needed by repetition draw detection.
// Keep track of position keys along the setup moves (from start position to the
// position just before to start searching). Needed by repetition draw detection.
Search::StateStackPtr SetupStates;

vector<PyObject*> observers;
Lock bestmoveLock;
WaitCondition bestmoveCondition;

void buildPosition(Position *p, Search::StateStackPtr states, const char *fen, PyObject *moves)
{
    if(strcmp(fen,"startpos")==0) fen=StartFEN;
    p->set(fen, false, Threads.main());

    // parse the move list
    int numMoves = PyList_Size(moves);
    for (int i=0; i<numMoves ; i++) {
        string moveStr( PyString_AsString( PyList_GetItem(moves, i)) );
        Move m;
        if((m = move_from_uci(*p, moveStr)) != MOVE_NONE)
        {
            states->push(StateInfo());
            p->do_move(m, states->top());
        }
        else
        {
            PyErr_SetString(PyExc_ValueError, (string("Invalid move '")+moveStr+"'").c_str());
        }
    }
}

}

extern "C" PyObject* stockfish_getOptions(PyObject* self)
{
    PyObject* dict = PyDict_New();
    for (UCI::OptionsMap::iterator iter = Options.begin(); iter != Options.end(); ++iter)
    {
        PyObject *dict_key=Py_BuildValue("s", (*iter).first.c_str());
        PyObject *dict_value=((*iter).second.type == "spin" ?
                              Py_BuildValue("(sssii)",(*iter).second.currentValue.c_str(),(*iter).second.type.c_str(),(*iter).second.defaultValue.c_str(),(*iter).second.min,(*iter).second.max):
                              Py_BuildValue("(sss)",(*iter).second.currentValue.c_str(),(*iter).second.type.c_str(),(*iter).second.defaultValue.c_str())
                             );
        PyDict_SetItem(dict,dict_key,dict_value);
    }
    return dict;
}

extern "C" PyObject* stockfish_info(PyObject* self)
{
    return Py_BuildValue("s", engine_info().c_str());
}

//INPUT: fen, list of moves
extern "C" PyObject* stockfish_key(PyObject* self, PyObject *args)
{
    PyObject *listObj;
    Position p;
    Search::StateStackPtr states = Search::StateStackPtr(new std::stack<StateInfo>());
    const char *fen;
    if (!PyArg_ParseTuple(args, "sO!", &fen,  &PyList_Type, &listObj)) {
        return NULL;
    }
    buildPosition(&p,states,fen,listObj);

    // This needs to be capital "K" so that it works both on 32 bit ARM devices and 64 bit machines.
    return Py_BuildValue("K", PolyglotBook::polyglot_key(p));
}

extern "C" PyObject* stockfish_stop(PyObject* self)
{
    if(Threads.main()->thinking)
    {
        Search::Signals.stop = true;
        Threads.main()->notify_one(); // Could be sleeping

        Py_BEGIN_ALLOW_THREADS
        cond_wait(bestmoveCondition,bestmoveLock);
        Py_END_ALLOW_THREADS
    }
    Py_RETURN_NONE;
}

extern "C" PyObject* stockfish_ponderhit(PyObject* self)
{
    if (Search::Signals.stopOnPonderhit)
        stockfish_stop(self);
    else
        Search::Limits.ponder = false;
    Py_RETURN_NONE;
}

extern "C" PyObject* stockfish_setOption(PyObject* self, PyObject *args)
{
    const char *name;
    PyObject *valueObj;
    if (!PyArg_ParseTuple(args, "sO", &name, &valueObj)) return NULL;

    if (Options.count(name))
        Options[name] = string(PyString_AsString(PyObject_Str(valueObj)));
    else
    {
        PyErr_SetString(PyExc_ValueError, (string("No such option ")+name+"'").c_str());
        return NULL;
    }
    Py_RETURN_NONE;
}

extern "C" PyObject* stockfish_addObserver(PyObject* self, PyObject *args)
{
    PyObject *observer;
    if (!PyArg_ParseTuple(args, "O", &observer)) return NULL;

    Py_INCREF(observer);
    observers.push_back(observer);
    Py_RETURN_NONE;
}

extern "C" PyObject* stockfish_removeObserver(PyObject* self, PyObject *args)
{
    PyObject *observer;
    if (!PyArg_ParseTuple(args, "O", &observer)) return NULL;

    observers.erase(remove(observers.begin(), observers.end(), observer), observers.end());
    Py_XDECREF(observer);
    Py_RETURN_NONE;
}

//INPUT fen
extern "C" PyObject* stockfish_legalMoves(PyObject* self, PyObject *args)
{
    PyObject* list = PyList_New(0);
    Position p;

    const char *fen=NULL;
    if (!PyArg_ParseTuple(args, "s", &fen)) return NULL;
    if(strcmp(fen,"startpos")==0) fen=StartFEN;
    p.set(fen, false, Threads.main());

    for (MoveList<LEGAL> it(p); *it; ++it)
    {
        PyObject *move=Py_BuildValue("s", move_to_uci(*it,false).c_str());
        PyList_Append(list, move);
        Py_XDECREF(move);
    }

    return list;
}

void stockfish_notifyObservers(string s)
{
    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();

    PyObject *arglist=Py_BuildValue("(s)", s.c_str());
    for (vector<PyObject*>::iterator it = observers.begin() ; it != observers.end(); ++it)
        PyObject_CallObject(*it, arglist);

    if(!s.compare(0,8,"bestmove")) cond_signal(bestmoveCondition);

    Py_DECREF(arglist);
    PyGILState_Release(gstate);
}

//Given a list of moves in CAN formats, it returns a list of moves in SAN format
//Input FEN, list of moves
extern "C" PyObject* stockfish_toSAN(PyObject* self, PyObject *args)
{
    PyObject* sanMoves = PyList_New(0), *moveList;
    stack<Move> moveStack;
    Search::StateStackPtr states = Search::StateStackPtr(new std::stack<StateInfo>());
    Position p;
    const char *fen;

    if (!PyArg_ParseTuple(args, "sO!", &fen,  &PyList_Type, &moveList)) {
        return NULL;
    }
    if(strcmp(fen,"startpos")==0) fen=StartFEN;
    p.set(fen, false, Threads.main());

    // parse the move list
    int numMoves = PyList_Size(moveList);
    for (int i=0; i<numMoves ; i++) {
        string moveStr( PyString_AsString( PyList_GetItem(moveList, i)) );
        Move m;
        if((m = move_from_uci(p, moveStr)) != MOVE_NONE)
        {
            //add to the san move list
            PyObject *move=Py_BuildValue("s", move_to_san(p,m).c_str());
            PyList_Append(sanMoves, move);
            Py_XDECREF(move);

            //do the move
            states->push(StateInfo());
            moveStack.push(m);
            p.do_move(m, states->top());
        }
        else
        {
            PyErr_SetString(PyExc_ValueError, (string("Invalid move '")+moveStr+"'").c_str());
            return NULL;
        }
    }
    return sanMoves;
}

//Given a list of moves in SAN formats, it returns a list of moves in CAN format
//Input FEN, list of moves
extern "C" PyObject* stockfish_toCAN(PyObject* self, PyObject *args)
{
    PyObject *canMoves = PyList_New(0), *moveList;
    stack<Move> moveStack;
    Search::StateStackPtr states = Search::StateStackPtr(new std::stack<StateInfo>());
    Position p;
    const char *fen;

    if (!PyArg_ParseTuple(args, "sO!", &fen,  &PyList_Type, &moveList)) {
        return NULL;
    }
    if(strcmp(fen,"startpos")==0) fen=StartFEN;
    p.set(fen, Options["UCI_Chess960"], Threads.main());

    // parse the move list
    int numMoves = PyList_Size(moveList);
    for (int i=0; i<numMoves ; i++)
    {
        string moveStr( PyString_AsString( PyList_GetItem(moveList, i)) );
        bool found=false;
        for (MoveList<LEGAL> it(p); *it; ++it)
        {
            if(!moveStr.compare(move_to_san(p,*it)))
            {
                PyObject *move=Py_BuildValue("s", move_to_uci(*it,false).c_str());
                //add to the can move list
                PyList_Append(canMoves, move);
                Py_XDECREF(move);

                //do the move
                states->push(StateInfo());
                moveStack.push(*it);
                p.do_move(*it, states->top());
                found=true;
                break;
            }
        }
        if(!found)
        {
            PyErr_SetString(PyExc_ValueError, (string("Invalid move '")+moveStr+"'").c_str());
            return NULL;
        }
    }

    return canMoves;
}

// go() is called when engine receives the "go" UCI command. The function sets
// the thinking time and other parameters from the input string, and starts
// the search.
extern "C" PyObject* stockfish_go(PyObject *self, PyObject *args, PyObject *kwargs) {
    Search::LimitsType limits;
    vector<Move> searchMoves;
    PyObject *listSearchMoves;
    PyObject *moveList;
    const char *fen=NULL;
    Position p;

    stockfish_stop(self);
    //cout<<"thinking:"<<Threads.main()->thinking<<endl;
    SetupStates = Search::StateStackPtr(new std::stack<StateInfo>());

    const char *kwlist[] = {"fen", "moves", "searchmoves", "wtime", "btime", "winc", "binc", "movestogo", "depth", "nodes", "movetime", "mate", "infinite", "ponder", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "sO!|O!iiiiiiiiiii", const_cast<char **>(kwlist), &fen, &PyList_Type, &moveList, &PyList_Type, &listSearchMoves,
                                     &(limits.time[WHITE]), &(limits.time[BLACK]), &(limits.inc[WHITE]), &(limits.inc[BLACK]),
                                     &(limits.movestogo), &(limits.depth), &(limits.nodes), &(limits.movetime), &(limits.mate), &(limits.infinite), &(limits.ponder)))
        return NULL;

    if(strcmp(fen,"startpos")==0) fen=StartFEN;
    p.set(fen, false, Threads.main());

    int numSearchMoves = PyList_Size(listSearchMoves);
    for (int i=0; i<numSearchMoves ; i++) {
        string moveStr( PyString_AsString( PyList_GetItem(listSearchMoves, i)) );
        Move m;
        if((m = move_from_uci(p, moveStr)) != MOVE_NONE) {
            searchMoves.push_back(m);
        }
    }

    // parse the move list
    int numMoves = PyList_Size(moveList);
    for (int i=0; i<numMoves ; i++) {
        string moveStr( PyString_AsString( PyList_GetItem(moveList, i)) );
        Move m;
        if((m = move_from_uci(p, moveStr)) != MOVE_NONE)
        {
            SetupStates->push(StateInfo());
            p.do_move(m, SetupStates->top());
        }
        else
        {
            PyErr_SetString(PyExc_ValueError, (string("Invalid move '")+moveStr+"'").c_str());
        }
    }

    Threads.start_thinking(p, limits, searchMoves, SetupStates);
    Py_RETURN_NONE;
}

//Given a fen and a list of moves, returns the FEN with all moves made
extern "C" PyObject* stockfish_getFEN(PyObject* self, PyObject *args)
{
    PyObject *moveList;
    const char *fen=NULL;
    Position p;

    if (!PyArg_ParseTuple(args, "sO!", &fen, &PyList_Type, &moveList)) return NULL;
    if(strcmp(fen,"startpos")==0) fen=StartFEN;
    p.set(fen, false, Threads.main());
    Search::StateStackPtr states = Search::StateStackPtr(new std::stack<StateInfo>());

    // parse the move list
    int numMoves = PyList_Size(moveList);
    for (int i=0; i<numMoves ; i++)
    {
        string moveStr( PyString_AsString( PyList_GetItem(moveList, i)) );


        Move m;
        if((m = move_from_uci(p, moveStr)) != MOVE_NONE)
        {
            states->push(StateInfo());
            p.do_move(m, states->top());
        }

    }

    return Py_BuildValue("s", p.fen().c_str());
}


static char stockfish_docs[] =
    "helloworld( ): Any message you want to put here!!\n";

static PyMethodDef stockfish_funcs[] = {
    {"add_observer", (PyCFunction)stockfish_addObserver, METH_VARARGS, stockfish_docs},
    {"remove_observer", (PyCFunction)stockfish_removeObserver, METH_VARARGS, stockfish_docs},
    //{"flip", (PyCFunction)stockfish_flip, METH_NOARGS, stockfish_docs},
    {"go", (PyCFunction)stockfish_go, METH_VARARGS | METH_KEYWORDS, stockfish_docs},
    {"info", (PyCFunction)stockfish_info, METH_NOARGS, stockfish_docs},
    {"key", (PyCFunction)stockfish_key, METH_VARARGS, stockfish_docs},
    {"legal_moves", (PyCFunction)stockfish_legalMoves, METH_VARARGS, stockfish_docs},
    {"get_fen", (PyCFunction)stockfish_getFEN, METH_VARARGS, stockfish_docs},
    {"to_can", (PyCFunction)stockfish_toCAN, METH_VARARGS, stockfish_docs},
    {"to_san", (PyCFunction)stockfish_toSAN, METH_VARARGS, stockfish_docs},
    {"ponderhit", (PyCFunction)stockfish_ponderhit, METH_NOARGS, stockfish_docs},
    //{"position", (PyCFunction)stockfish_position, METH_VARARGS, stockfish_docs},
    {"set_option", (PyCFunction)stockfish_setOption, METH_VARARGS, stockfish_docs},
    {"get_options", (PyCFunction)stockfish_getOptions, METH_NOARGS, stockfish_docs},
    {"stop", (PyCFunction)stockfish_stop, METH_NOARGS, stockfish_docs},
    {NULL}
};

PyMODINIT_FUNC initstockfish(void)
{
    Py_InitModule3("stockfish", stockfish_funcs, "Extension module example!");

    UCI::init(Options);
    Bitboards::init();
    Position::init();
    Bitbases::init_kpk();
    Search::init();
    Pawns::init();
    Eval::init();
    Threads.init();
    TT.set_size(Options["Hash"]);

    lock_init(bestmoveLock);
    cond_init(bestmoveCondition);

    // Make sure the GIL has been created since we need to acquire it in our
    // callback to safely call into the python application.
    if (! PyEval_ThreadsInitialized()) {
        PyEval_InitThreads();
    }
}
