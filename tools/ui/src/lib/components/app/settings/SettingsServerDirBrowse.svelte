<script lang="ts">
	import { FolderPlus } from '@lucide/svelte';
	import { Button } from '$lib/components/ui/button';
	import { Checkbox } from '$lib/components/ui/checkbox';
	import * as Dialog from '$lib/components/ui/dialog';
	import { Input } from '$lib/components/ui/input';
	import { API_UPLOADS } from '$lib/constants';
	import { apiFetch } from '$lib/utils';

	interface DirEntry {
		name: string;
		path: string;
		writable: boolean;
	}

	interface FileEntry {
		name: string;
		path: string;
		size: number;
	}

	interface DirList {
		entries: DirEntry[];
		files?: FileEntry[];
		parent: string;
		path: string;
		writable: boolean;
	}

	interface Props {
		onSelect: (path: string) => void;
		open?: boolean;
		startPath?: string;
	}

	let { onSelect, open = $bindable(false), startPath = '' }: Props = $props();

	let listing = $state<DirList | null>(null);
	let error = $state('');
	let newName = $state('');
	let loading = $state(false);
	let selected = $state<Record<string, boolean>>({});
	let deleting = $state(false);

	const files = $derived(listing?.files ?? []);
	const selectedPaths = $derived(files.filter((file) => selected[file.path]).map((file) => file.path));
	const allSelected = $derived(files.length > 0 && selectedPaths.length === files.length);
	const someSelected = $derived(selectedPaths.length > 0 && !allSelected);

	function formatSize(size: number): string {
		if (size < 1024) {
			return `${size} B`;
		}
		if (size < 1024 * 1024) {
			return `${(size / 1024).toFixed(1)} KB`;
		}
		return `${(size / (1024 * 1024)).toFixed(1)} MB`;
	}

	async function load(path: string) {
		loading = true;
		error = '';
		selected = {};

		try {
			const query = path ? `?path=${encodeURIComponent(path)}` : '';
			listing = await apiFetch<DirList>(`${API_UPLOADS.DIRS}${query}`);
		} catch (err) {
			error = err instanceof Error ? err.message : 'Failed to list folders';
			listing = null;
		} finally {
			loading = false;
		}
	}

	$effect(() => {
		if (open) {
			void load(startPath.trim());
		}
	});

	function toggleFile(path: string, checked: boolean) {
		selected = { ...selected, [path]: checked };
	}

	function toggleSelectAll(checked: boolean) {
		const next: Record<string, boolean> = {};
		if (checked) {
			for (const file of files) {
				next[file.path] = true;
			}
		}
		selected = next;
	}

	async function deleteSelected() {
		if (selectedPaths.length === 0 || !listing?.path) {
			return;
		}

		deleting = true;
		error = '';

		try {
			await apiFetch(API_UPLOADS.DELETE_FILES, {
				body: JSON.stringify({ paths: selectedPaths }),
				method: 'POST'
			});
			await load(listing.path);
		} catch (err) {
			error = err instanceof Error ? err.message : 'Failed to delete files';
		} finally {
			deleting = false;
		}
	}

	async function createFolder() {
		if (!listing?.path || !newName.trim()) {
			return;
		}

		error = '';

		try {
			const created = await apiFetch<{ path: string }>(API_UPLOADS.DIRS, {
				body: JSON.stringify({ name: newName.trim(), parent: listing.path }),
				method: 'POST'
			});

			newName = '';
			await load(listing.path);

			if (created.path) {
				await load(created.path);
			}
		} catch (err) {
			error = err instanceof Error ? err.message : 'Failed to create folder';
		}
	}
</script>

<Dialog.Root bind:open>
	<Dialog.Content class="sm:max-w-lg">
		<Dialog.Header>
			<Dialog.Title>Choose attachment folder</Dialog.Title>
			<Dialog.Description>
				Only directories the llama-server process can write are listed. Files in the current folder
				can be selected and deleted.
			</Dialog.Description>
		</Dialog.Header>

		{#if listing?.path}
			<p class="break-all font-mono text-xs text-muted-foreground">{listing.path}</p>
		{:else}
			<p class="text-xs text-muted-foreground">Writable roots</p>
		{/if}

		{#if error}
			<p class="text-sm text-destructive">{error}</p>
		{/if}

		<div class="max-h-72 overflow-y-auto rounded-md border">
			{#if loading}
				<p class="p-3 text-sm text-muted-foreground">Loading…</p>
			{:else if listing}
				{#if listing.parent}
					<button
						class="block w-full px-3 py-2 text-left text-sm hover:bg-accent"
						onclick={() => load(listing.parent)}
						type="button"
					>
						..
					</button>
				{:else if listing.path}
					<button
						class="block w-full px-3 py-2 text-left text-sm hover:bg-accent"
						onclick={() => load('')}
						type="button"
					>
						..
					</button>
				{/if}

				{#each listing.entries as entry (entry.path)}
					<button
						class="block w-full px-3 py-2 text-left text-sm hover:bg-accent"
						onclick={() => load(entry.path)}
						type="button"
					>
						{entry.name}/
					</button>
				{/each}

				{#if files.length > 0}
					<div class="flex items-center gap-2 border-t px-3 py-2">
						<Checkbox
							aria-label="Select all files"
							checked={allSelected}
							indeterminate={someSelected}
							onCheckedChange={(value) => toggleSelectAll(value === true)}
						/>
						<span class="text-xs text-muted-foreground">Select all files</span>
					</div>
					{#each files as file (file.path)}
						<label class="flex items-center gap-2 px-3 py-2 text-sm hover:bg-accent">
							<Checkbox
								checked={Boolean(selected[file.path])}
								onCheckedChange={(value) => toggleFile(file.path, value === true)}
							/>
							<span class="min-w-0 flex-1 truncate" title={file.name}>{file.name}</span>
							<span class="shrink-0 text-xs text-muted-foreground">{formatSize(file.size)}</span>
						</label>
					{/each}
				{:else if listing.entries.length === 0 && listing.path}
					<p class="p-3 text-sm text-muted-foreground">No writable subfolders or files</p>
				{/if}
			{/if}
		</div>

		{#if listing?.writable && listing.path}
			<div class="flex gap-2">
				<Input
					class="flex-1"
					onkeydown={(event) => {
						if (event.key === 'Enter') {
							event.preventDefault();
							void createFolder();
						}
					}}
					placeholder="New subfolder name"
					bind:value={newName}
				/>
				<Button onclick={() => createFolder()} type="button" variant="secondary">
					<FolderPlus class="h-4 w-4" />
					Create
				</Button>
			</div>
		{/if}

		<Dialog.Footer>
			<Button onclick={() => (open = false)} type="button" variant="ghost">Cancel</Button>
			<Button
				disabled={selectedPaths.length === 0 || deleting}
				onclick={() => deleteSelected()}
				type="button"
				variant="destructive"
			>
				{deleting ? 'Deleting…' : `Delete selected (${selectedPaths.length})`}
			</Button>
			<Button
				disabled={!listing?.writable || !listing.path}
				onclick={() => {
					if (listing?.path) {
						onSelect(listing.path);
						open = false;
					}
				}}
				type="button"
			>
				Use this folder
			</Button>
		</Dialog.Footer>
	</Dialog.Content>
</Dialog.Root>
