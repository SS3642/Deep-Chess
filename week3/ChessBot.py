import chess
import json
import time
class EngineSolver:
    def __init__(self):
        self.INF = int(1e9)
        self.MATE_SCORE = int(1e8)

    def solve_puzzle(self, fen: str, depth: int) -> list[chess.Move]:
        board = chess.Board(fen)
        score, pv_line = self._alpha_beta(board, depth, -self.INF, self.INF)
        return pv_line

    def _alpha_beta(self, board: chess.Board, depth: int, alpha: int, beta: int) -> tuple[int, list[chess.Move]]:
        if board.is_checkmate():
            return -self.MATE_SCORE + depth, []
            
        if board.is_stalemate() or board.is_insufficient_material() or board.is_repetition():
            return 0, []
            
        if depth == 0:
            return 0, []

        max_score = -self.INF
        best_line = []
        for move in board.legal_moves:
            board.push(move)
            score, line = self._alpha_beta(board, depth - 1, -beta, -alpha)
            score = -score 
            board.pop()
            if score > max_score:
                max_score = score
                best_line = [move] + line
            alpha = max(alpha, max_score)
            if alpha >= beta:
                break 
        return max_score, best_line

if __name__ == "__main__":
    engine = EngineSolver()
    
    # 1. Open and parse the JSON file
    # Replace 'mate_in_2.json' with the actual path to your file if it's in a different folder
    with open('mate_in_2.json', 'r') as file:
        puzzles = json.load(file)
        
    total_puzzles = len(puzzles)
    correct_solutions = 0
    
    print(f"Successfully loaded {total_puzzles} puzzles. Booting up engine...\n")
    print("-" * 50)
    
    # Start the global timer for benchmarking
    start_time = time.time()
    
    # 2. Iterate through every puzzle in the dictionary
    for index, (fen, expected_solution) in enumerate(puzzles.items(), start=1):
        
        # 3. Ask the engine to calculate the solution (Depth 4 for Mate in 2)
        winning_line = engine.solve_puzzle(fen, depth=6)
        
        # 4. Convert the engine's list of Moves to Standard Algebraic Notation (SAN)
        board = chess.Board(fen)
        engine_san_sequence = []
        
        for move in winning_line:
            engine_san_sequence.append(board.san(move))
            board.push(move)
            
        # Join the list into a single string to match the JSON format
        engine_solution_string = " ".join(engine_san_sequence)
        
        # 5. Verify the engine's answer against the JSON answer key
        if engine_solution_string == expected_solution:
            print(f"[{index}/{total_puzzles}] ✅ SOLVED | FEN: {fen}")
            correct_solutions += 1
        else:
            print(f"[{index}/{total_puzzles}] ❌ FAILED | FEN: {fen}")
            print(f"    Expected: {expected_solution}")
            print(f"    Engine:   {engine_solution_string}")

    # 6. Print final benchmarking statistics
    end_time = time.time()
    elapsed_time = end_time - start_time
    
    print("-" * 50)
    print("🎯 TEST RUN COMPLETE")
    print(f"Score: {correct_solutions} / {total_puzzles}")
    print(f"Total Time: {elapsed_time:.2f} seconds")
    print(f"Average Time per Puzzle: {(elapsed_time / total_puzzles):.4f} seconds")